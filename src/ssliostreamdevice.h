#ifndef SSLIOSTREAMDEVICE_H
#define SSLIOSTREAMDEVICE_H

#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/version.hpp>

//
// IOStream device that speaks SSL but can also speak non-SSL
//

template <typename Protocol>
class SSLIOStreamDevice : public boost::iostreams::device<boost::iostreams::bidirectional>
{
private:
	bool fNeedHandshake;
	bool fUseSSL;
	boost::asio::ssl::stream<typename Protocol::socket>& stream;

public:
	SSLIOStreamDevice(boost::asio::ssl::stream<typename Protocol::socket> &streamIn, bool fUseSSLIn);

	void handshake(boost::asio::ssl::stream_base::handshake_type role);
	std::streamsize read(char* s, std::streamsize n);
	std::streamsize write(const char* s, std::streamsize n);
	bool connect(const std::string& server, const std::string& port);
};

#endif // SSLIOSTREAMDEVICE_H
