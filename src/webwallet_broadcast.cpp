#include <queue>

#include <boost/thread.hpp>

#include "util.h"
#include "webwallet.h"
#include "webwallet_broadcast.h"

std::mutex									m_action_lock;
std::mutex									m_connection_lock;
boost::thread*								m_thread;
std::condition_variable						m_action_cond;
std::queue<DigitalNote::Webwallet::action>	m_actions;

namespace DigitalNote
{
namespace Webwallet
{

broadcast::broadcast()
{
	// Initialize Asio Transport
	DigitalNote::Webwallet::ext_server.init_asio();

	// Register handler callbacks
	DigitalNote::Webwallet::ext_server.set_open_handler(
		websocketpp::lib::bind(
			&broadcast::on_open,
			this,
			websocketpp::lib::placeholders::_1
		)
	);
	
	DigitalNote::Webwallet::ext_server.set_close_handler(
		websocketpp::lib::bind(
			&broadcast::on_close,
			this,
			websocketpp::lib::placeholders::_1
		)
	);
}

void broadcast::run(uint16_t port)
{
	LogPrint("webwallet", "webwallet: run is starting. \n");

	// listen on specified port
	DigitalNote::Webwallet::ext_server.listen(port);

	// Start the server accept loop
	DigitalNote::Webwallet::ext_server.start_accept();

	LogPrint("webwallet", "webwallet: Creating processing thread \n");
	
	m_thread = new boost::thread(boost::bind(&process_messages));

	// Start the ASIO io_service run loop
	try
	{
		LogPrint("webwallet", "webwallet: ext_server is starting. \n");
		
		DigitalNote::Webwallet::ext_server.run();
		
		LogPrint("webwallet", "webwallet: ext_server is now closed. \n");
	}
	catch (const std::exception &e)
	{
		LogPrint("webwallet", "webwallet: ERROR: Failed to start websocket. \n");
		LogPrint("webwallet", e.what());
	}
	
	m_thread->join();
}

void broadcast::stop()
{
	LogPrint("webwallet", "webwallet: Requesting websocket to stop.\n");
	
	std::lock_guard<std::mutex> guard(m_action_lock);
	
	m_actions.push(action(STOP_COMMAND));
	m_action_cond.notify_all();
}

void broadcast::on_open(websocketpp::connection_hdl hdl)
{
	{
		std::lock_guard<std::mutex> guard(m_action_lock);
		
		m_actions.push(action(SUBSCRIBE,hdl));
	}
	
	m_action_cond.notify_all();
}

void broadcast::on_close(websocketpp::connection_hdl hdl)
{
	{
		std::lock_guard<std::mutex> guard(m_action_lock);
		m_actions.push(action(UNSUBSCRIBE,hdl));
	}
	
	m_action_cond.notify_all();
}

void broadcast::sendMessage(const std::string &msg)
{
	if (!DigitalNote::Webwallet::ext_connector_enabled)
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

void broadcast::process_messages()
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
			
			DigitalNote::Webwallet::ext_connections.insert(a.hdl);
		}
		else if (a.type == UNSUBSCRIBE)
		{
			LogPrint("webwallet", "webwallet: Connection SUBSCRIBE.\n");
			
			std::lock_guard<std::mutex> guard(m_connection_lock);
			
			DigitalNote::Webwallet::ext_connections.erase(a.hdl);
		}
		else if (a.type == MESSAGE)
		{
			LogPrint("webwallet", "webwallet: Connection MESSAGE.\n");
			std::lock_guard<std::mutex> guard(m_connection_lock);

			DigitalNote::Webwallet::connections::iterator it;
			
			for (it = DigitalNote::Webwallet::ext_connections.begin(); it != DigitalNote::Webwallet::ext_connections.end(); ++it)
			{
				websocketpp::lib::error_code ec;
				DigitalNote::Webwallet::ext_server.send(*it, a.msg, websocketpp::frame::opcode::text, ec);
			}
		}
		else if (a.type == STOP_COMMAND)
		{
			LogPrint("webwallet", "webwallet: STOP_COMMAND.\n");
			
			try
			{
				DigitalNote::Webwallet::ext_server.stop_listening();
				LogPrint("webwallet", "webwallet: Websocket server stopped listening. \n");
			}
			catch (const std::exception &e)
			{
				LogPrint("webwallet", "webwallet: ERROR: Failed to stop websocket server. \n");
				LogPrint("webwallet", e.what());
			}

			std::lock_guard<std::mutex> guard(m_connection_lock);
			
			{
				DigitalNote::Webwallet::connections::iterator it;
				
				for (it = DigitalNote::Webwallet::ext_connections.begin(); it != DigitalNote::Webwallet::ext_connections.end(); ++it)
				{
					websocketpp::connection_hdl hdl = *it;
					DigitalNote::Webwallet::ext_server.pause_reading(hdl);
					DigitalNote::Webwallet::ext_server.close(hdl, websocketpp::close::status::going_away, "");
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

} // namespace Webwallet
} // namespace DigitalNote
