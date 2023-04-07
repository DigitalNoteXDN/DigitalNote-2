#include <stdint.h>
#include <boost/algorithm/string.hpp>

#include "util.h"
#include "enums/httpstatuscode.h"
#include "json/json_spirit_writer_template.h"

#include "rpcprotocol.h"

// Number of bytes to allocate and read at most at once in post data
const size_t POST_READ_SIZE = 256 * 1024;

//
// HTTP protocol
//
// This ain't Apache.  We're just using HTTP header for the length field
// and to be compatible with other JSON-RPC implementations.
//

std::string HTTPPost(const std::string& strMsg, const mapRequestHeaders_t& mapRequestHeaders)
{
	std::ostringstream s;

	s << "POST / HTTP/1.1\r\n"
	  << "User-Agent: DigitalNote-json-rpc/" << FormatFullVersion() << "\r\n"
	  << "Host: 127.0.0.1\r\n"
	  << "Content-Type: application/json\r\n"
	  << "Content-Length: " << strMsg.size() << "\r\n"
	  << "Connection: close\r\n"
	  << "Accept: application/json\r\n";

	for(const pairRequestHeaders_t& item : mapRequestHeaders)
	{
		s << item.first << ": " << item.second << "\r\n";
	}

	s << "\r\n" << strMsg;

	return s.str();
}

static std::string rfc1123Time()
{
	return DateTimeStrFormat("%a, %d %b %Y %H:%M:%S +0000", GetTime());
}

std::string HTTPReply(int nStatus, const std::string& strMsg, bool keepalive)
{
	if (nStatus == HTTP_UNAUTHORIZED)
	{
		return strprintf(
			"HTTP/1.0 401 Authorization Required\r\n"
			"Date: %s\r\n"
			"Server: DigitalNote-json-rpc/%s\r\n"
			"WWW-Authenticate: Basic realm=\"jsonrpc\"\r\n"
			"Content-Type: text/html\r\n"
			"Content-Length: 296\r\n"
			"\r\n"
			"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
			"\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
			"<HTML>\r\n"
			"<HEAD>\r\n"
			"<TITLE>Error</TITLE>\r\n"
			"<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-1'>\r\n"
			"</HEAD>\r\n"
			"<BODY><H1>401 Unauthorized.</H1></BODY>\r\n"
			"</HTML>\r\n",
			rfc1123Time(),
			FormatFullVersion()
		);
	}

	const char *cStatus;
	
	switch(nStatus)
	{
		case HTTP_OK:
			cStatus = "OK";
			break;
		
		case HTTP_BAD_REQUEST:
			cStatus = "Bad Request";
			break;
		
		case HTTP_FORBIDDEN:
			cStatus = "Forbidden";
			break;
			
		case HTTP_NOT_FOUND:
			cStatus = "Not Found";
			break;
			
		case HTTP_INTERNAL_SERVER_ERROR:
			cStatus = "Internal Server Error";
			break;
		
		default:
			cStatus = "";
			break;
	}

	return strprintf(
		"HTTP/1.1 %d %s\r\n"
		"Date: %s\r\n"
		"Connection: %s\r\n"
		"Content-Length: %u\r\n"
		"Content-Type: application/json\r\n"
		"Server: DigitalNote-json-rpc/%s\r\n"
		"\r\n"
		"%s",
		nStatus,
		cStatus,
		rfc1123Time(),
		keepalive ? "keep-alive" : "close",
		strMsg.size(),
		FormatFullVersion(),
		strMsg
	);
}

bool ReadHTTPRequestLine(std::basic_istream<char>& stream, int &proto, std::string& http_method, std::string& http_uri)
{
	std::string str;
	std::vector<std::string> vWords;

	getline(stream, str);
	boost::split(vWords, str, boost::is_any_of(" "));

	if (vWords.size() < 2)
	{
		return false;
	}

	// HTTP methods permitted: GET, POST
	http_method = vWords[0];

	if (http_method != "GET" && http_method != "POST")
	{
		return false;
	}

	// HTTP URI must be an absolute path, relative to current host
	http_uri = vWords[1];

	if (http_uri.size() == 0 || http_uri[0] != '/')
	{
		return false;
	}

	// parse proto, if present
	std::string strProto = "";

	if (vWords.size() > 2)
	{
		strProto = vWords[2];
	}

	proto = 0;
	const char *ver = strstr(strProto.c_str(), "HTTP/1.");

	if (ver != NULL)
	{
		proto = atoi(ver+7);
	}

	return true;
}

int ReadHTTPStatus(std::basic_istream<char>& stream, int &proto)
{
	std::string str;
	std::vector<std::string> vWords;

	getline(stream, str);
	boost::split(vWords, str, boost::is_any_of(" "));

	if (vWords.size() < 2)
	{
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	proto = 0;
	const char *ver = strstr(str.c_str(), "HTTP/1.");

	if (ver != NULL)
	{
		proto = atoi(ver + 7);
	}

	return atoi(vWords[1].c_str());
}

int ReadHTTPHeaders(std::basic_istream<char>& stream, mapHeadersRet_t& mapHeadersRet)
{
	int nLen = 0;

	while (true)
	{
		std::string str;
		
		std::getline(stream, str);
		
		if (str.empty() || str == "\r")
		{
			break;
		}
		
		std::string::size_type nColon = str.find(":");
		
		if (nColon != std::string::npos)
		{
			std::string strHeader = str.substr(0, nColon);
			
			boost::trim(strHeader);
			boost::to_lower(strHeader);
			
			std::string strValue = str.substr(nColon+1);
			boost::trim(strValue);
			
			mapHeadersRet[strHeader] = strValue;
			
			if (strHeader == "content-length")
			{
				nLen = atoi(strValue.c_str());
			}
		}
	}

	return nLen;
}

int ReadHTTPMessage(std::basic_istream<char>& stream, mapHeadersRet_t& mapHeadersRet,
		std::string& strMessageRet, int nProto, size_t max_size)
{
	mapHeadersRet.clear();
	strMessageRet = "";

	// Read header
	int nLen = ReadHTTPHeaders(stream, mapHeadersRet);

	if (nLen < 0 || (size_t)nLen > max_size)
	{
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	// Read message
	if (nLen > 0)
	{
		std::vector<char> vch;
		size_t ptr = 0;
		
		while (ptr < (size_t)nLen)
		{
			size_t bytes_to_read = std::min((size_t)nLen - ptr, POST_READ_SIZE);
			
			vch.resize(ptr + bytes_to_read);
			stream.read(&vch[ptr], bytes_to_read);
			
			if (!stream) // Connection lost while reading
			{
				return HTTP_INTERNAL_SERVER_ERROR;
			}
			
			ptr += bytes_to_read;
		}
		
		strMessageRet = std::string(vch.begin(), vch.end());
	}

	std::string sConHdr = mapHeadersRet["connection"];

	if ((sConHdr != "close") && (sConHdr != "keep-alive"))
	{
		if (nProto >= 1)
		{
			mapHeadersRet["connection"] = "keep-alive";
		}
		else
		{
			mapHeadersRet["connection"] = "close";
		}
	}

	return HTTP_OK;
}

//
// JSON-RPC protocol.  DigitalNote speaks version 1.0 for maximum compatibility,
// but uses JSON-RPC 1.1/2.0 standards for parts of the 1.0 standard that were
// unspecified (HTTP errors and contents of 'error').
//
// 1.0 spec: http://json-rpc.org/wiki/specification
// 1.2 spec: http://groups.google.com/group/json-rpc/web/json-rpc-over-http
// http://www.codeproject.com/KB/recipes/JSON_Spirit.aspx
//

std::string JSONRPCRequest(const std::string& strMethod, const json_spirit::Array& params, const json_spirit::Value& id)
{
	json_spirit::Object request;

	request.push_back(json_spirit::Pair("method", strMethod));
	request.push_back(json_spirit::Pair("params", params));
	request.push_back(json_spirit::Pair("id", id));

	return write_string(json_spirit::Value(request), false) + "\n";
}

json_spirit::Object JSONRPCReplyObj(const json_spirit::Value& result, const json_spirit::Value& error, const json_spirit::Value& id)
{
	json_spirit::Object reply;

	if (error.type() != json_spirit::null_type)
	{
		reply.push_back(json_spirit::Pair("result", json_spirit::Value::null));
	}
	else
	{
		reply.push_back(json_spirit::Pair("result", result));
	}

	reply.push_back(json_spirit::Pair("error", error));
	reply.push_back(json_spirit::Pair("id", id));

	return reply;
}

std::string JSONRPCReply(const json_spirit::Value& result, const json_spirit::Value& error, const json_spirit::Value& id)
{
	json_spirit::Object reply = JSONRPCReplyObj(result, error, id);

	return write_string(json_spirit::Value(reply), false) + "\n";
}

json_spirit::Object JSONRPCError(int code, const std::string& message)
{
	json_spirit::Object error;

	error.push_back(json_spirit::Pair("code", code));
	error.push_back(json_spirit::Pair("message", message));

	return error;
}

