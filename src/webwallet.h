#ifndef WEBWALLET_H
#define WEBWALLET_H

#include <set>
#include <string>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

namespace DigitalNote
{
	namespace Webwallet
	{
		class broadcast;
		
		typedef websocketpp::server<websocketpp::config::asio> server;
		typedef std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> connections;

		enum action_type
		{
			SUBSCRIBE,
			UNSUBSCRIBE,
			MESSAGE,
			STOP_COMMAND
		};
		
		struct action
		{
			Webwallet::action_type type;
			websocketpp::connection_hdl hdl;
			std::string msg;
			
			action(Webwallet::action_type t, websocketpp::connection_hdl h) : type(t), hdl(h)
			{
				
			}
			
			action(Webwallet::action_type t, websocketpp::connection_hdl h, const std::string &m): type(t), hdl(h), msg(m)
			{
				
			}
			
			action(Webwallet::action_type t, const std::string &m): type(t), msg(m)
			{
				
			}
			
			action(Webwallet::action_type t): type(t)
			{
				
			}
		};
		
		extern Webwallet::server		ext_server;
		extern Webwallet::connections	ext_connections;
		extern Webwallet::broadcast		ext_broadcast;
		
		extern bool ext_mode;
		extern bool ext_connector_enabled;
		
		bool Start(bool fDontStart);
		bool Shutdown();
		void ThreadWebsocket();
		void SendUpdate(const std::string &msg);

		void Subscribe();
		void Unsubscribe();
	} // namespace Webwallet
} // namespace DigitalNote

#endif // WEBWALLET_H
