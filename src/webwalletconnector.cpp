//
// Created by vgulkevic on 06/10/2020.
//

#include <iostream>
#include <set>
#include <functional>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/server.hpp>

#include "json/json_spirit_writer_template.h"
#include "util.h"
#include "smsg_extern.h"
#include "smsg_extern_signal.h"

#include "webwalletconnector.h"

boost::thread_group threadGroupWebWalletConnector;

/* on_open insert websocketpp::connection_hdl into channel
 * on_close remove websocketpp::connection_hdl from channel
 * on_message queue send to all channels
 */

enum action_type {
	SUBSCRIBE,
	UNSUBSCRIBE,
	MESSAGE,
	STOP_COMMAND
};

struct action {
	action(action_type t, websocketpp::connection_hdl h) : type(t), hdl(h) {}
	action(action_type t, websocketpp::connection_hdl h, const std::string &m): type(t), hdl(h), msg(m) {}
	action(action_type t, const std::string &m): type(t), msg(m) {}
	action(action_type t): type(t) {}

	action_type type;
	websocketpp::connection_hdl hdl;
	std::string msg;
};

typedef websocketpp::server<websocketpp::config::asio> server;
typedef std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl> > con_list;

bool fWebWalletConnectorEnabled = false;
bool fWebWalletMode = false;

std::mutex m_action_lock;
std::mutex m_connection_lock;
std::condition_variable m_action_cond;
std::queue<action> m_actions;
server m_server;
con_list m_connections;
boost::thread* m_thread;

class broadcast_server
{
public:
	broadcast_server()
	{
		// Initialize Asio Transport
		m_server.init_asio();

		// Register handler callbacks
		m_server.set_open_handler(
			websocketpp::lib::bind(
				&broadcast_server::on_open,
				this,
				websocketpp::lib::placeholders::_1
			)
		);
		
		m_server.set_close_handler(
			websocketpp::lib::bind(
				&broadcast_server::on_close,
				this,
				websocketpp::lib::placeholders::_1
			)
		);
	}

	void run(uint16_t port)
	{
		LogPrint("webwallet", "webwallet: run is starting. \n");

		// listen on specified port
		m_server.listen(port);

		// Start the server accept loop
		m_server.start_accept();

		LogPrint("webwallet", "webwallet: Creating processing thread \n");
		
		m_thread = new boost::thread(boost::bind(&process_messages));

		// Start the ASIO io_service run loop
		try
		{
			LogPrint("webwallet", "webwallet: m_server is starting. \n");
			
			m_server.run();
			
			LogPrint("webwallet", "webwallet: m_server is now closed. \n");
		}
		catch (const std::exception &e)
		{
			LogPrint("webwallet", "webwallet: ERROR: Failed to start websocket. \n");
			LogPrint("webwallet", e.what());
		}
		
		m_thread->join();
	}

	void stop()
	{
		LogPrint("webwallet", "webwallet: Requesting websocket to stop.\n");
		
		std::lock_guard<std::mutex> guard(m_action_lock);
		
		m_actions.push(action(STOP_COMMAND));
		m_action_cond.notify_all();
	}

	void on_open(websocketpp::connection_hdl hdl)
	{
		{
			std::lock_guard<std::mutex> guard(m_action_lock);
			
			m_actions.push(action(SUBSCRIBE,hdl));
		}
		
		m_action_cond.notify_all();
	}

	void on_close(websocketpp::connection_hdl hdl)
	{
		{
			std::lock_guard<std::mutex> guard(m_action_lock);
			m_actions.push(action(UNSUBSCRIBE,hdl));
		}
		
		m_action_cond.notify_all();
	}

	void sendMessage(const std::string &msg)
	{
		if (!fWebWalletConnectorEnabled)
		{
			return;
		}

		LogPrint("webwallet", "webwallet: Sending sendMessage to queue \n");
		LogPrint("webwallet", "webwallet: %s \n", msg);
		
		std::lock_guard<std::mutex> guard(m_action_lock);
		
		m_actions.push(action(MESSAGE, msg));
		
		LogPrint("webwallet", "webwallet: m_actions size %d .\n", m_actions.size());
		LogPrint("webwallet", "webwallet: notyfing all .\n");
		
		m_action_cond.notify_all();
	}

	static void process_messages()
	{
		while(true)
		{
			LogPrint("webwallet", "webwallet: Locked m_action_lock.\n");
			
			std::unique_lock<std::mutex> lock(m_action_lock);

			while(m_actions.empty())
			{
				LogPrint("webwallet", "webwallet: Waiting for new actions.\n");
				
				m_action_cond.wait(lock);
			}

			action a = m_actions.front();
			m_actions.pop();

			lock.unlock();

			if (a.type == SUBSCRIBE)
			{
				LogPrint("webwallet", "webwallet: Connection SUBSCRIBE.\n");
				
				std::lock_guard<std::mutex> guard(m_connection_lock);
				
				m_connections.insert(a.hdl);
			}
			else if (a.type == UNSUBSCRIBE)
			{
				LogPrint("webwallet", "webwallet: Connection SUBSCRIBE.\n");
				
				std::lock_guard<std::mutex> guard(m_connection_lock);
				
				m_connections.erase(a.hdl);
			}
			else if (a.type == MESSAGE)
			{
				LogPrint("webwallet", "webwallet: Connection MESSAGE.\n");
				std::lock_guard<std::mutex> guard(m_connection_lock);

				con_list::iterator it;
				
				for (it = m_connections.begin(); it != m_connections.end(); ++it)
				{
					websocketpp::lib::error_code ec;
					m_server.send(*it, a.msg, websocketpp::frame::opcode::text, ec);
				}
			}
			else if (a.type == STOP_COMMAND)
			{
				LogPrint("webwallet", "webwallet: STOP_COMMAND.\n");
				
				try
				{
					m_server.stop_listening();
					LogPrint("webwallet", "webwallet: Websocket server stopped listening. \n");
				}
				catch (const std::exception &e)
				{
					LogPrint("webwallet", "webwallet: ERROR: Failed to stop websocket server. \n");
					LogPrint("webwallet", e.what());
				}

				std::lock_guard<std::mutex> guard(m_connection_lock);
				
				{
					con_list::iterator it;
					
					for (it = m_connections.begin(); it != m_connections.end(); ++it)
					{
						websocketpp::connection_hdl hdl = *it;
						m_server.pause_reading(hdl);
						m_server.close(hdl, websocketpp::close::status::going_away, "");
					}
				}

				LogPrint("webwallet", "webwallet: Sent close request to all connections.\n");
				
				break;
			}
			else {
				LogPrint("webwallet", "webwallet: undefined COMMAND.\n");
				// undefined.
			}
		}
		
		LogPrint("webwallet", "webwallet: Leaving process_messages.\n");
	}
};

broadcast_server server_instance;
void ThreadWebsocketServer()
{
	try
	{
		LogPrint("webwallet", "webwallet: ThreadWebsocketServer before run .\n");
		server_instance.run(7778);
	}
	catch (websocketpp::exception const & e)
	{
		LogPrint("webwallet", "webwallet: ERROR: Failed to start ThreadWebsocketServer websocket thread. \n");
		LogPrint("webwallet", e.what());
	}

	LogPrint("webwallet", "webwallet: ThreadWebsocketServer finishing.\n");
}

/** called from AppInit2() in init.cpp */
bool WebWalletConnectorStart(bool fDontStart)
{
	if (!fDontStart)
	{
		LogPrint("webwallet", "webwallet: Web wallet connector not started.\n");
		
		return false;
	}

	fWebWalletMode = true;
	fWebWalletConnectorEnabled = true;

	threadGroupWebWalletConnector.create_thread(
		boost::bind(
			&TraceThread<void (*)()>,
			"webwallet",
			&ThreadWebsocketServer
		)
	);

	subscribeToCoreSignals();

	LogPrint("webwallet", "webwallet: Web wallet connector starting.\n");

	return true;
}

bool WebWalletConnectorShutdown()
{
	if (!fWebWalletConnectorEnabled)
	{
		return false;
	}

	unsubscribeFromCoreSignals();
	fWebWalletConnectorEnabled = false;
	server_instance.stop();

	LogPrint("webwallet", "webwallet: Waiting for threads.\n");
	threadGroupWebWalletConnector.interrupt_all();
	threadGroupWebWalletConnector.join_all();

	LogPrint("webwallet", "webwallet: Stopping web wallet connector.\n");

	return true;
}

void SendUpdateToWebWallet(const std::string &msg)
{
	if (fWebWalletConnectorEnabled)
	{
		server_instance.sendMessage(msg);
	}
}

void NotifySecMsgInbox(json_spirit::Object& msg)
{
	LogPrint("webwallet", "webwallet: Signal for inbox message. \n");

	server_instance.sendMessage(write_string(json_spirit::Value(msg), false));
}

void NotifySecMsgOutbox(json_spirit::Object& msg)
{
	LogPrint("webwallet", "webwallet: Signal for outbox message. \n");

	server_instance.sendMessage(write_string(json_spirit::Value(msg), false));
}

void subscribeToCoreSignals()
{
	LogPrint("webwallet", "webwallet: subscribeToCoreSignals \n");

	// Connect signals
	DigitalNote::SMSG::ext_signal_NotifyInboxChangedJson.connect(&NotifySecMsgInbox);
	DigitalNote::SMSG::ext_signal_NotifyOutboxChangedJson.connect(&NotifySecMsgOutbox);
}

void unsubscribeFromCoreSignals()
{
	LogPrint("webwallet", "webwallet: unsubscribeFromCoreSignals \n");

	// Disconnect signals
	DigitalNote::SMSG::ext_signal_NotifyInboxChangedJson.disconnect(&NotifySecMsgInbox);
	DigitalNote::SMSG::ext_signal_NotifyOutboxChangedJson.disconnect(&NotifySecMsgOutbox);
}

