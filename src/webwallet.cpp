#include <boost/thread.hpp>
//#include <boost/bind.hpp>
//#include <boost/thread/mutex.hpp>
//#include <boost/thread/condition_variable.hpp>

#include "json/json_spirit_writer_template.h"

#include "util.h"
#include "smsg_extern.h"
#include "smsg_extern_signal.h"

#include "webwallet.h"
#include "webwallet_broadcast.h"

boost::thread_group g_thread;

namespace DigitalNote
{
namespace Webwallet
{

Webwallet::server		ext_server;
Webwallet::connections	ext_connections;
Webwallet::broadcast	ext_broadcast;

bool 					ext_mode = false;
bool					ext_connector_enabled = false;

/* on_open insert websocketpp::connection_hdl into channel
 * on_close remove websocketpp::connection_hdl from channel
 * on_message queue send to all channels
 */

/** called from AppInit2() in init.cpp */
bool Start(bool fDontStart)
{
	if (!fDontStart)
	{
		LogPrint("webwallet", "webwallet: Web wallet connector not started.\n");
		
		return false;
	}

	DigitalNote::Webwallet::ext_mode = true;
	DigitalNote::Webwallet::ext_connector_enabled = true;

	g_thread.create_thread(
		boost::bind(
			&TraceThread<void (*)()>,
			"webwallet",
			&DigitalNote::Webwallet::ThreadWebsocket
		)
	);

	DigitalNote::Webwallet::Subscribe();

	LogPrint("webwallet", "webwallet: Web wallet connector starting.\n");

	return true;
}

bool Shutdown()
{
	if (!DigitalNote::Webwallet::ext_connector_enabled)
	{
		return false;
	}

	DigitalNote::Webwallet::Unsubscribe();
	DigitalNote::Webwallet::ext_connector_enabled = false;
	DigitalNote::Webwallet::ext_broadcast.stop();

	LogPrint("webwallet", "webwallet: Waiting for threads.\n");
	g_thread.interrupt_all();
	g_thread.join_all();

	LogPrint("webwallet", "webwallet: Stopping web wallet connector.\n");

	return true;
}

void ThreadWebsocket()
{
	try
	{
		LogPrint("webwallet", "webwallet: ThreadWebsocketServer before run .\n");
		DigitalNote::Webwallet::ext_broadcast.run(7778);
	}
	catch (websocketpp::exception const & e)
	{
		LogPrint("webwallet", "webwallet: ERROR: Failed to start ThreadWebsocketServer websocket thread. \n");
		LogPrint("webwallet", e.what());
	}

	LogPrint("webwallet", "webwallet: ThreadWebsocketServer finishing.\n");
}

void SendUpdate(const std::string &msg)
{
	if (DigitalNote::Webwallet::ext_connector_enabled)
	{
		DigitalNote::Webwallet::ext_broadcast.sendMessage(msg);
	}
}

void NotifySecMsgInbox(json_spirit::Object& msg)
{
	LogPrint("webwallet", "webwallet: Signal for inbox message. \n");

	DigitalNote::Webwallet::ext_broadcast.sendMessage(write_string(json_spirit::Value(msg), false));
}

void NotifySecMsgOutbox(json_spirit::Object& msg)
{
	LogPrint("webwallet", "webwallet: Signal for outbox message. \n");

	DigitalNote::Webwallet::ext_broadcast.sendMessage(write_string(json_spirit::Value(msg), false));
}

void Subscribe()
{
	LogPrint("webwallet", "webwallet: Subscribe \n");

	// Connect signals
	DigitalNote::SMSG::ext_signal_NotifyInboxChangedJson.connect(&DigitalNote::Webwallet::NotifySecMsgInbox);
	DigitalNote::SMSG::ext_signal_NotifyOutboxChangedJson.connect(&DigitalNote::Webwallet::NotifySecMsgOutbox);
}

void Unsubscribe()
{
	LogPrint("webwallet", "webwallet: unsubscribeFromCoreSignals \n");

	// Disconnect signals
	DigitalNote::SMSG::ext_signal_NotifyInboxChangedJson.disconnect(&DigitalNote::Webwallet::NotifySecMsgInbox);
	DigitalNote::SMSG::ext_signal_NotifyOutboxChangedJson.disconnect(&DigitalNote::Webwallet::NotifySecMsgOutbox);
}

} // namespace Webwallet
} // namespace DigitalNote



