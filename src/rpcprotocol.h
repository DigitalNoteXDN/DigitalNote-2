#ifndef RPCPROTOCOL_H
#define RPCPROTOCOL_H

#include <list>
#include <map>
#include <stdint.h>
#include <string>

#include "json/json_spirit_value.h"

typedef std::map<std::string, std::string> mapRequestHeaders_t;
typedef std::pair<std::string, std::string> pairRequestHeaders_t;

typedef std::map<std::string, std::string> mapHeadersRet_t;

std::string HTTPPost(const std::string& strMsg, const mapRequestHeaders_t& mapRequestHeaders);
std::string HTTPReply(int nStatus, const std::string& strMsg, bool keepalive);
bool ReadHTTPRequestLine(std::basic_istream<char>& stream, int &proto, std::string& http_method, std::string& http_uri);
int ReadHTTPStatus(std::basic_istream<char>& stream, int &proto);
int ReadHTTPHeaders(std::basic_istream<char>& stream, mapHeadersRet_t& mapHeadersRet);
int ReadHTTPMessage(std::basic_istream<char>& stream, mapHeadersRet_t& mapHeadersRet,
		std::string& strMessageRet, int nProto, size_t max_size);

std::string JSONRPCRequest(const std::string& strMethod, const json_spirit::Array& params, const json_spirit::Value& id);
json_spirit::Object JSONRPCReplyObj(const json_spirit::Value& result, const json_spirit::Value& error, const json_spirit::Value& id);
std::string JSONRPCReply(const json_spirit::Value& result, const json_spirit::Value& error, const json_spirit::Value& id);
json_spirit::Object JSONRPCError(int code, const std::string& message);

#endif // RPCPROTOCOL_H
