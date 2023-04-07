#ifndef WEBWALLET_BROADCAST_H
#define WEBWALLET_BROADCAST_H

namespace DigitalNote
{
	namespace Webwallet
	{
		class broadcast
		{
		public:
			broadcast();
			
			void run(uint16_t port);
			void stop();
			void on_open(websocketpp::connection_hdl hdl);
			void on_close(websocketpp::connection_hdl hdl);
			void sendMessage(const std::string &msg);
			static void process_messages();
		};
	} // namespace Webwallet
} // namespace DigitalNote

#endif // WEBWALLET_BROADCAST_H
