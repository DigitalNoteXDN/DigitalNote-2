#include "boost_ioservices.h"

#include "ssliostreamdevice.h"

template <typename Protocol>
SSLIOStreamDevice<Protocol>::SSLIOStreamDevice(boost::asio::ssl::stream<typename Protocol::socket> &streamIn, bool fUseSSLIn) : stream(streamIn)
{
	fUseSSL = fUseSSLIn;
	fNeedHandshake = fUseSSLIn;
}

template <typename Protocol>
void SSLIOStreamDevice<Protocol>::handshake(boost::asio::ssl::stream_base::handshake_type role)
{
	if (!fNeedHandshake)
	{
		return;
	}
	
	fNeedHandshake = false;
	stream.handshake(role);
}

template <typename Protocol>
std::streamsize SSLIOStreamDevice<Protocol>::read(char* s, std::streamsize n)
{
	handshake(boost::asio::ssl::stream_base::server); // HTTPS servers read first
	
	if (fUseSSL)
	{
		return stream.read_some(boost::asio::buffer(s, n));
	}
	
	return stream.next_layer().read_some(boost::asio::buffer(s, n));
}

template <typename Protocol>
std::streamsize SSLIOStreamDevice<Protocol>::write(const char* s, std::streamsize n)
{
	handshake(boost::asio::ssl::stream_base::client); // HTTPS clients write first
	
	if (fUseSSL)
	{
		return boost::asio::write(stream, boost::asio::buffer(s, n));
	}
	
	return boost::asio::write(stream.next_layer(), boost::asio::buffer(s, n));
}



template <typename Protocol>
bool SSLIOStreamDevice<Protocol>::connect(const std::string& server, const std::string& port)
{
	// Boost Version < 1.70 handling (Updated) - Thank you https://github.com/g1itch
	boost::asio::ip::tcp::resolver resolver(GetIOService(stream));
	// Boost Version < 1.70 handling (Depricated) - Thank you Mino#8171
	// boost::asio::ip::tcp::resolver resolver(stream.get_io_service());
	// boost::asio::ip::tcp::resolver resolver(GET_IO_SERVICE(stream));
	boost::asio::ip::tcp::resolver::query query(server.c_str(), port.c_str());
	boost::asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
	boost::asio::ip::tcp::resolver::iterator end;
	boost::system::error_code error = boost::asio::error::host_not_found;
	
	while (error && endpoint_iterator != end)
	{
		stream.lowest_layer().close();
		stream.lowest_layer().connect(*endpoint_iterator++, error);
	}
	
	if (error)
	{
		return false;
	}
	
	return true;
}

// Define template type
template class SSLIOStreamDevice<boost::asio::ip::tcp>;

