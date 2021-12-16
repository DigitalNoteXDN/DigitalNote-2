#ifndef SSLIOSTREAMDEVICE_H
#define SSLIOSTREAMDEVICE_H

#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/version.hpp>

// Boost Support for 1.70+ (Updated)
// Thank you https://github.com/g1itch
#if BOOST_VERSION >= 107000
	#define GetIOService(s) ((boost::asio::io_context&)(s).get_executor().context())
	#define GetIOServiceFromPtr(s) ((boost::asio::io_context&)(s->get_executor().context())) // this one
	typedef boost::asio::io_context ioContext;

#else
	#define GetIOService(s) ((s).get_io_service())
	#define GetIOServiceFromPtr(s) ((s)->get_io_service())
	typedef boost::asio::io_service ioContext;
#endif

// Boost Support for 1.70+ (Depricated)
// Thank you Mino#8171
// ====== BOOST SUCKS ========
// #if BOOST_VERSION >= 107000
// #define GET_IO_SERVICE(s) ((boost::asio::io_context&)(s).get_executor().context())
// #else
// #define GET_IO_SERVICE(s) ((s).get_io_service())
// #endif
//  ===== RETROCOMPATIBILITY SHOULD NOT BE AN OPTION ======

//
// IOStream device that speaks SSL but can also speak non-SSL
//

template <typename Protocol>
class SSLIOStreamDevice : public boost::iostreams::device<boost::iostreams::bidirectional>
{
public:
	SSLIOStreamDevice(boost::asio::ssl::stream<typename Protocol::socket> &streamIn, bool fUseSSLIn);

	void handshake(boost::asio::ssl::stream_base::handshake_type role);
	std::streamsize read(char* s, std::streamsize n);
	std::streamsize write(const char* s, std::streamsize n);
	bool connect(const std::string& server, const std::string& port);

private:
    bool fNeedHandshake;
    bool fUseSSL;
    boost::asio::ssl::stream<typename Protocol::socket>& stream;
};

#endif // SSLIOSTREAMDEVICE_H
