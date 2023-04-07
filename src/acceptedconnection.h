#ifndef ACCEPTEDCONNECTION_H
#define ACCEPTEDCONNECTION_H

#include <iostream>

#include "ssliostreamdevice.h"
#include "types/iocontext.h"

class AcceptedConnection
{
public:
	virtual ~AcceptedConnection()
	{
		
	}

	virtual std::iostream& stream() = 0;
	virtual std::string peer_address_to_string() const = 0;
	virtual void close() = 0;
};

template <typename Protocol>
class AcceptedConnectionImpl : public AcceptedConnection
{
private:
	SSLIOStreamDevice<Protocol> _d;
	boost::iostreams::stream<SSLIOStreamDevice<Protocol>> _stream;

public:
	typename Protocol::endpoint peer;
	boost::asio::ssl::stream<typename Protocol::socket> sslStream;
	
	AcceptedConnectionImpl(ioContext& io_context, boost::asio::ssl::context &context, bool fUseSSL);
	
	virtual std::iostream& stream();
	virtual std::string peer_address_to_string() const;
	virtual void close();
};

#endif // ACCEPTEDCONNECTION_H
