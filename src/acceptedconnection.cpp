#include "acceptedconnection.h"

template <typename Protocol>
AcceptedConnectionImpl<Protocol>::AcceptedConnectionImpl(ioContext& io_context, boost::asio::ssl::context &context, bool fUseSSL)
		: sslStream(io_context, context), _d(sslStream, fUseSSL), _stream(_d)
{
	
}

template <typename Protocol>
std::iostream& AcceptedConnectionImpl<Protocol>::stream()
{
	return _stream;
}

template <typename Protocol>
std::string AcceptedConnectionImpl<Protocol>::peer_address_to_string() const
{
	return peer.address().to_string();
}

template <typename Protocol>
void AcceptedConnectionImpl<Protocol>::close()
{
	_stream.close();
}

// Generate template
template class AcceptedConnectionImpl<boost::asio::ip::tcp>;
