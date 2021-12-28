#include <set>
#include <stdint.h>

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"

#include "ssliostreamdevice.h"
#include "rpcprotocol.h"
#include "enums/httpstatuscode.h"
#include "util.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "ui_translate.h"
#include "types/iocontext.h"
#include "main_const.h"

#include "rpcclient.h"

json_spirit::Object CallRPC(const std::string& strMethod, const json_spirit::Array& params)
{
	if (mapArgs["-rpcuser"] == "" && mapArgs["-rpcpassword"] == "")
	{
		throw std::runtime_error(strprintf(
			ui_translate("You must set rpcpassword=<password> in the configuration file:\n%s\n"
			  "If the file does not exist, create it with owner-readable-only file permissions."),
				GetConfigFile().string()
			)
		);
	}

	// Connect to localhost
	bool fUseSSL = GetBoolArg("-rpcssl", false);
	ioContext io_context;
	boost::asio::ssl::context context(boost::asio::ssl::context::sslv23);
	context.set_options(boost::asio::ssl::context::no_sslv2);
	boost::asio::ssl::stream<boost::asio::ip::tcp::socket> sslStream(io_context, context);
	SSLIOStreamDevice<boost::asio::ip::tcp> d(sslStream, fUseSSL);
	boost::iostreams::stream< SSLIOStreamDevice<boost::asio::ip::tcp> > stream(d);

	bool fWait = GetBoolArg("-rpcwait", false); // -rpcwait means try until server has started

	do
	{
		bool fConnected = d.connect(GetArg("-rpcconnect", "127.0.0.1"), GetArg("-rpcport", itostr(Params().RPCPort())));
		
		if (fConnected)
		{
			break;
		}
		
		if (fWait)
		{
			MilliSleep(1000);
		}
		else
		{
			throw std::runtime_error("couldn't connect to server");
		}
	} while (fWait);

	// HTTP basic authentication
	std::string strUserPass64 = EncodeBase64(mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"]);
	std::map<std::string, std::string> mapRequestHeaders;
	mapRequestHeaders["Authorization"] = std::string("Basic ") + strUserPass64;

	// Send request
	std::string strRequest = JSONRPCRequest(strMethod, params, 1);
	std::string strPost = HTTPPost(strRequest, mapRequestHeaders);
	stream << strPost << std::flush;

	// Receive HTTP reply status
	int nProto = 0;
	int nStatus = ReadHTTPStatus(stream, nProto);

	// Receive HTTP reply message headers and body
	std::map<std::string, std::string> mapHeaders;
	std::string strReply;
	ReadHTTPMessage(stream, mapHeaders, strReply, nProto, MAX_MESSAGE_SIZE);

	if (nStatus == HTTP_UNAUTHORIZED)
	{
		throw std::runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
	}
	else if (nStatus >= 400 && nStatus != HTTP_BAD_REQUEST && nStatus != HTTP_NOT_FOUND && nStatus != HTTP_INTERNAL_SERVER_ERROR)
	{
		throw std::runtime_error(strprintf("server returned HTTP error %d", nStatus));
	}
	else if (strReply.empty())
	{
		throw std::runtime_error("no response from server");
	}

	// Parse reply
	json_spirit::Value valReply;

	if (!read_string(strReply, valReply))
	{
		throw std::runtime_error("couldn't parse reply from server");
	}

	const json_spirit::Object& reply = valReply.get_obj();

	if (reply.empty())
	{
		throw std::runtime_error("expected reply to have result, error and id properties");
	}

	return reply;
}

class CRPCConvertParam
{
public:
	std::string methodName;            // method whose params want conversion
	int paramIdx;                      // 0-based idx of param to convert
};

static const CRPCConvertParam vRPCConvertParams[] =
{
	{ "stop", 0 },
	{ "getaddednodeinfo", 0 },
	{ "sendtoaddress", 1 },
	{ "settxfee", 0 },
	{ "getreceivedbyaddress", 1 },
	{ "getreceivedbyaccount", 1 },
	{ "listreceivedbyaddress", 0 },
	{ "listreceivedbyaddress", 1 },
	{ "listreceivedbyaddress", 2 },
	{ "listreceivedbyaccount", 0 },
	{ "listreceivedbyaccount", 1 },
	{ "listreceivedbyaccount", 2 },
	{ "getbalance", 1 },
	{ "getbalance", 2 },
	{ "getblock", 1 },
	{ "getblockbynumber", 0 },
	{ "getblockbynumber", 1 },
	{ "getblockhash", 0 },
	{ "cclistcoins", 0 },
	{ "move", 2 },
	{ "move", 3 },
	{ "sendfrom", 2 },
	{ "sendfrom", 3 },
	{ "listtransactions", 1 },
	{ "listtransactions", 2 },
	{ "listtransactions", 3 },
	{ "listaccounts", 0 },
	{ "listaccounts", 1 },
	{ "walletpassphrase", 1 },
	{ "walletpassphrase", 2 },
	{ "getblocktemplate", 0 },
	{ "listsinceblock", 1 },
	{ "listsinceblock", 2 },
	{ "sendalert", 2 },
	{ "sendalert", 3 },
	{ "sendalert", 4 },
	{ "sendalert", 5 },
	{ "sendalert", 6 },
	{ "sendmany", 1 },
	{ "sendmany", 2 },
	{ "reservebalance", 0 },
	{ "reservebalance", 1 },
	{ "createmultisig", 0 },
	{ "createmultisig", 1 },
	{ "addmultisigaddress", 0 },
	{ "addmultisigaddress", 1 },
	{ "listunspent", 0 },
	{ "listunspent", 1 },
	{ "listunspent", 2 },
	{ "getrawtransaction", 1 },
	{ "createrawtransaction", 0 },
	{ "createrawtransaction", 1 },
	{ "signrawtransaction", 1 },
	{ "signrawtransaction", 2 },
	{ "keypoolrefill", 0 },
	{ "importprivkey", 2 },
	{ "importaddress", 2 },
	{ "checkkernel", 0 },
	{ "checkkernel", 1 },
	{ "setban", 2 },
	{ "setban", 3 },
	{ "sendtostealthaddress", 1 },
	{ "searchrawtransactions", 1 },
	{ "searchrawtransactions", 2 },
	{ "searchrawtransactions", 3 },
};

class CRPCConvertTable
{
private:
	std::set<std::pair<std::string, int>> members;

public:
	CRPCConvertTable();

	bool convert(const std::string& method, int idx)
	{
		return (members.count(std::make_pair(method, idx)) > 0);
	}
};

CRPCConvertTable::CRPCConvertTable()
{
	const unsigned int n_elem = (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));

	for (unsigned int i = 0; i < n_elem; i++)
	{
		members.insert(std::make_pair(vRPCConvertParams[i].methodName, vRPCConvertParams[i].paramIdx));
	}
}

static CRPCConvertTable rpcCvtTable;

// Convert strings to command-specific RPC representation
json_spirit::Array RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
	json_spirit::Array params;

	for (unsigned int idx = 0; idx < strParams.size(); idx++)
	{
		const std::string& strVal = strParams[idx];

		// insert string value directly
		if (!rpcCvtTable.convert(strMethod, idx))
		{
			params.push_back(strVal);
		}
		// parse string as JSON, insert bool/number/object/etc. value
		else
		{
			json_spirit::Value jVal;
			
			if (!read_string(strVal, jVal))
			{
				throw std::runtime_error(std::string("Error parsing JSON:")+strVal);
			}
			
			params.push_back(jVal);
		}
	}

	return params;
}

int CommandLineRPC(int argc, char *argv[])
{
	std::string strPrint;
	int nRet = 0;

	try
	{
		// Skip switches
		while (argc > 1 && IsSwitchChar(argv[1][0]))
		{
			argc--;
			argv++;
		}

		// Method
		if (argc < 2)
		{
			throw std::runtime_error("too few parameters");
		}
		
		std::string strMethod = argv[1];

		// Parameters default to strings
		std::vector<std::string> strParams(&argv[2], &argv[argc]);
		json_spirit::Array params = RPCConvertValues(strMethod, strParams);

		// Execute
		json_spirit::Object reply = CallRPC(strMethod, params);

		// Parse reply
		const json_spirit::Value& result = find_value(reply, "result");
		const json_spirit::Value& error  = find_value(reply, "error");

		if (error.type() != json_spirit::null_type)
		{
			// Error
			strPrint = "error: " + write_string(error, false);
			int code = find_value(error.get_obj(), "code").get_int();
			nRet = abs(code);
		}
		else
		{
			// Result
			if (result.type() == json_spirit::null_type)
			{
				strPrint = "";
			}
			else if (result.type() == json_spirit::str_type)
			{
				strPrint = result.get_str();
			}
			else
			{
				strPrint = write_string(result, true);
			}
		}
	}
	catch (boost::thread_interrupted)
	{
		throw;
	}
	catch (std::exception& e)
	{
		strPrint = std::string("error: ") + e.what();
		
		nRet = 87;
	}
	catch (...)
	{
		PrintException(NULL, "CommandLineRPC()");
	}

	if (strPrint != "")
	{
		fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str());
	}

	return nRet;
}

