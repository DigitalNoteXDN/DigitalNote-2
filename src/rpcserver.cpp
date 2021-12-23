// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include "util.h"
#include "enums/httpstatuscode.h"
#include "enums/rpcerrorcode.h"
#include "main_const.h"
#include "uint/uint256.h"
#include "init.h"
#include "ssliostreamdevice.h"
#include "rpcvelocity.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "base58.h"
#include "main_extern.h"
#include "thread.h"
#include "ui_interface.h"
#include "ui_translate.h"
#include "types/iocontext.h"
#include "boost_ioservicess.h"

#ifdef ENABLE_WALLET
#include "cwallet.h"
#endif

#include "rpcserver.h"

// RETROCOMPATIBILITY SHOULD NOT BE AN OPTION

static std::string strRPCUserColonPass;

// These are created by StartRPCThreads, destroyed in StopRPCThreads
static ioContext* rpc_io_service = NULL;
static std::map<std::string, boost::shared_ptr<boost::asio::deadline_timer> > deadlineTimers;
static boost::asio::ssl::context* rpc_ssl_context = NULL;
static boost::thread_group* rpc_worker_group = NULL;

void RPCTypeCheck(const json_spirit::Array& params, const std::list<json_spirit::Value_type>& typesExpected, bool fAllowNull)
{
    unsigned int i = 0;
    for(json_spirit::Value_type t : typesExpected)
    {
        if (params.size() <= i)
		{
            break;
		}
		
        const json_spirit::Value& v = params[i];
        if (!((v.type() == t) || (fAllowNull && (v.type() == json_spirit::null_type))))
        {
            std::string err = strprintf(
				"Expected type %s, got %s",
				json_spirit::Value_type_name[t],
				json_spirit::Value_type_name[v.type()]
			);
			
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
		
        i++;
    }
}

void RPCTypeCheck(const json_spirit::Object& o, const std::map<std::string, json_spirit::Value_type>& typesExpected, bool fAllowNull)
{
    for(const std::pair<std::string, json_spirit::Value_type>& t : typesExpected)
    {
        const json_spirit::Value& v = find_value(o, t.first);
        if (!fAllowNull && v.type() == json_spirit::null_type)
		{
            throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing %s", t.first));
		}
		
        if (!((v.type() == t.second) || (fAllowNull && (v.type() == json_spirit::null_type))))
        {
            std::string err = strprintf(
				"Expected type %s for %s, got %s",
				json_spirit::Value_type_name[t.second],
				t.first,
				json_spirit::Value_type_name[v.type()]
			);
			
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
    }
}

int64_t AmountFromValue(const json_spirit::Value& value)
{
    double dAmount = value.get_real();
    if (dAmount <= 0.0 || dAmount > MAX_SINGLE_TX)
	{
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
    }
	
	CAmount nAmount = roundint64(dAmount * COIN);
    if (!MoneyRange(nAmount))
	{
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
	}
	
    return nAmount;
}

json_spirit::Value ValueFromAmount(int64_t amount)
{
    return (double)amount / (double)COIN;
}

//
// Utilities: convert hex-encoded json_spirit::Values
// (throws error if not hex).
//
uint256 ParseHashV(const json_spirit::Value& v, const std::string &strName)
{
    std::string strHex;
    if (v.type() == json_spirit::str_type)
	{
        strHex = v.get_str();
    }
	
	if (!IsHex(strHex)) // Note: IsHex("") is false
	{
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName+" must be hexadecimal string (not '"+strHex+"')");
    }
	
	uint256 result;
    result.SetHex(strHex);
    
	return result;
}

uint256 ParseHashO(const json_spirit::Object& o, const std::string &strKey)
{
    return ParseHashV(find_value(o, strKey), strKey);
}

std::vector<unsigned char> ParseHexV(const json_spirit::Value& v, const std::string &strName)
{
    std::string strHex;
    if (v.type() == json_spirit::str_type)
	{
        strHex = v.get_str();
    }
	
	if (!IsHex(strHex))
	{
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName+" must be hexadecimal string (not '"+strHex+"')");
    }
	
	return ParseHex(strHex);
}

std::vector<unsigned char> ParseHexO(const json_spirit::Object& o, const std::string &strKey)
{
    return ParseHexV(find_value(o, strKey), strKey);
}

///
/// Note: This interface may still be subject to change.
///

std::string CRPCTable::help(const std::string &strCommand) const
{
    std::string strRet;
    std::set<rpcfn_type> setDone;
    
	for (std::map<std::string, const CRPCCommand*>::const_iterator mi = mapCommands.begin(); mi != mapCommands.end(); ++mi)
    {
        const CRPCCommand *pcmd = mi->second;
        std::string strMethod = mi->first;
        
		// We already filter duplicates, but these deprecated screw up the sort order
        if (strMethod.find("label") != std::string::npos)
		{
            continue;
        }
		
		if (strCommand != "" && strMethod != strCommand)
		{
            continue;
		}
		
#ifdef ENABLE_WALLET
        if (pcmd->reqWallet && !pwalletMain)
		{
            continue;
		}
#endif

        try
        {
            json_spirit::Array params;
            rpcfn_type pfn = pcmd->actor;
            
			if (setDone.insert(pfn).second)
			{
                (*pfn)(params, true);
			}
        }
        catch (std::exception& e)
        {
            // Help text is returned in an exception
            std::string strHelp = std::string(e.what());
            if (strCommand == "")
			{
                if (strHelp.find('\n') != std::string::npos)
				{
                    strHelp = strHelp.substr(0, strHelp.find('\n'));
				}
            }
			
			strRet += strHelp + "\n";
        }
    }
    
	if (strRet == "")
	{
        strRet = strprintf("help: unknown command: %s\n", strCommand);
    }
	
	strRet = strRet.substr(0,strRet.size()-1);
    
	return strRet;
}

json_spirit::Value help(const json_spirit::Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
	{
        throw std::runtime_error(
            "help [command]\n"
            "List commands, or get help for a command."
		);
	}
	
    std::string strCommand;
    if (params.size() > 0)
	{
        strCommand = params[0].get_str();
	}
	
    return tableRPC.help(strCommand);
}

json_spirit::Value stop(const json_spirit::Array& params, bool fHelp)
{
    // Accept the deprecated and ignored 'detach' boolean argument
    if (fHelp || params.size() > 1)
	{
        throw std::runtime_error(
            "stop\n"
            "Stop DigitalNote server."
		);
    }
	
	// Shutdown will take long enough that the response should get back
    StartShutdown();
    
	return "DigitalNote server stopping";
}

//
// Call Table
//
static const CRPCCommand vRPCCommands[] =
{ //  name                      actor (function)         okSafeMode threadSafe reqWallet
  //  ------------------------  -----------------------  ---------- ---------- ---------
    { "help",                   &help,                   true,      true,      false },
    { "stop",                   &stop,                   true,      true,      false },
    { "getbestblockhash",       &getbestblockhash,       true,      false,     false },
    { "getblockcount",          &getblockcount,          true,      false,     false },
    { "getconnectioncount",     &getconnectioncount,     true,      false,     false },
    { "getpeerinfo",            &getpeerinfo,            true,      false,     false },
    { "addnode",                &addnode,                true,      true,      false },
    { "getaddednodeinfo",       &getaddednodeinfo,       true,      true,      false },
    { "ping",                   &ping,                   true,      false,     false },
    { "setban",                 &setban,                 true,      false,     false },
    { "listbanned",             &listbanned,             true,      false,     false },
    { "clearbanned",            &clearbanned,            true,      false,     false },
    { "getnettotals",           &getnettotals,           true,      true,      false },
    { "getdifficulty",          &getdifficulty,          true,      false,     false },
    { "getinfo",                &getinfo,                true,      false,     false },
    { "getvelocityinfo",        &getvelocityinfo,        true,      false,     false },
    { "getrawmempool",          &getrawmempool,          true,      false,     false },
    { "getblock",               &getblock,               false,     false,     false },
    { "getblockbynumber",       &getblockbynumber,       false,     false,     false },
    { "getblockhash",           &getblockhash,           false,     false,     false },
    { "getrawtransaction",      &getrawtransaction,      false,     false,     false },
    { "createrawtransaction",   &createrawtransaction,   false,     false,     false },
    { "decoderawtransaction",   &decoderawtransaction,   false,     false,     false },
    { "decodescript",           &decodescript,           false,     false,     false },
    { "signrawtransaction",     &signrawtransaction,     false,     false,     false },
    { "sendrawtransaction",     &sendrawtransaction,     false,     false,     false },
    { "getcheckpoint",          &getcheckpoint,          true,      false,     false },
    { "sendalert",              &sendalert,              false,     false,     false },
    { "validateaddress",        &validateaddress,        true,      false,     false },
    { "validatepubkey",         &validatepubkey,         true,      false,     false },
    { "verifymessage",          &verifymessage,          false,     false,     false },
    { "searchrawtransactions",  &searchrawtransactions,  false,     false,     false },

/* Masternode features */
    { "spork",                  &spork,                  true,      false,      false },
    { "masternode",             &masternode,             true,      false,      true },
    { "masternodelist",         &masternodelist,         true,      false,      false },

#ifdef ENABLE_WALLET
    { "getmininginfo",          &getmininginfo,          true,      false,     false },
    { "getstakinginfo",         &getstakinginfo,         true,      false,     false },
    { "getnewaddress",          &getnewaddress,          true,      false,     true },
    { "getnewpubkey",           &getnewpubkey,           true,      false,     true },
    { "getaccountaddress",      &getaccountaddress,      true,      false,     true },
    { "setaccount",             &setaccount,             true,      false,     true },
    { "getaccount",             &getaccount,             false,     false,     true },
    { "getaddressesbyaccount",  &getaddressesbyaccount,  true,      false,     true },
    { "sendtoaddress",          &sendtoaddress,          false,     false,     true },
    { "getreceivedbyaddress",   &getreceivedbyaddress,   false,     false,     true },
    { "getreceivedbyaccount",   &getreceivedbyaccount,   false,     false,     true },
    { "listreceivedbyaddress",  &listreceivedbyaddress,  false,     false,     true },
    { "listreceivedbyaccount",  &listreceivedbyaccount,  false,     false,     true },
    { "backupwallet",           &backupwallet,           true,      false,     true },
    { "keypoolrefill",          &keypoolrefill,          true,      false,     true },
    { "walletpassphrase",       &walletpassphrase,       true,      false,     true },
    { "walletpassphrasechange", &walletpassphrasechange, false,     false,     true },
    { "walletlock",             &walletlock,             true,      false,     true },
    { "encryptwallet",          &encryptwallet,          false,     false,     true },
    { "getbalance",             &getbalance,             false,     false,     true },
    { "move",                   &movecmd,                false,     false,     true },
    { "sendfrom",               &sendfrom,               false,     false,     true },
    { "sendmany",               &sendmany,               false,     false,     true },
    { "addmultisigaddress",     &addmultisigaddress,     false,     false,     true },
    { "addredeemscript",        &addredeemscript,        false,     false,     true },
    { "gettransaction",         &gettransaction,         false,     false,     true },
    { "listtransactions",       &listtransactions,       false,     false,     true },
    { "listaddressgroupings",   &listaddressgroupings,   false,     false,     true },
    { "signmessage",            &signmessage,            false,     false,     true },
    { "getwork",                &getwork,                true,      false,     true },
    { "getworkex",              &getworkex,              true,      false,     true },
    { "listaccounts",           &listaccounts,           false,     false,     true },
    { "getblocktemplate",       &getblocktemplate,       true,      false,     false },
    { "submitblock",            &submitblock,            false,     false,     false },
    { "listsinceblock",         &listsinceblock,         false,     false,     true },
    { "dumpprivkey",            &dumpprivkey,            false,     false,     true },
    { "dumpwallet",             &dumpwallet,             true,      false,     true },
    { "importprivkey",          &importprivkey,          false,     false,     true },
    { "importwallet",           &importwallet,           false,     false,     true },
    { "importaddress",          &importaddress,          false,     false,     true },
    { "listunspent",            &listunspent,            false,     false,     true },
    { "cclistcoins",            &cclistcoins,            false,     false,     true },
    { "settxfee",               &settxfee,               false,     false,     true },
    { "getsubsidy",             &getsubsidy,             true,      true,      false },
    { "getstakesubsidy",        &getstakesubsidy,        true,      true,      false },
    { "reservebalance",         &reservebalance,         false,     true,      true },
    { "createmultisig",         &createmultisig,         true,      true,      false },
    { "checkwallet",            &checkwallet,            false,     true,      true },
    { "repairwallet",           &repairwallet,           false,     true,      true },
    { "resendtx",               &resendtx,               false,     true,      true },
    { "makekeypair",            &makekeypair,            false,     true,      false },
    { "checkkernel",            &checkkernel,            true,      false,     true },
    { "getnewstealthaddress",   &getnewstealthaddress,   false,     false,     true },
    { "liststealthaddresses",   &liststealthaddresses,   false,     false,     true },
    { "scanforalltxns",         &scanforalltxns,         false,     false,     false },
    { "scanforstealthtxns",     &scanforstealthtxns,     false,     false,     false },
    { "importstealthaddress",   &importstealthaddress,   false,     false,     true },
    { "sendtostealthaddress",   &sendtostealthaddress,   false,     false,     true },
    { "smsgenable",             &smsgenable,             false,     false,     false },
    { "smsgdisable",            &smsgdisable,            false,     false,     false },
    { "smsglocalkeys",          &smsglocalkeys,          false,     false,     false },
    { "smsgoptions",            &smsgoptions,            false,     false,     false },
    { "smsgscanchain",          &smsgscanchain,          false,     false,     false },
    { "smsgscanbuckets",        &smsgscanbuckets,        false,     false,     false },
    { "smsgaddkey",             &smsgaddkey,             false,     false,     false },
    { "smsggetpubkey",          &smsggetpubkey,          false,     false,     false },
    { "smsgsend",               &smsgsend,               false,     false,     false },
    { "smsgsendanon",           &smsgsendanon,           false,     false,     false },
    { "smsginbox",              &smsginbox,              false,     false,     false },
    { "smsgoutbox",             &smsgoutbox,             false,     false,     false },
    { "smsgbuckets",            &smsgbuckets,            false,     false,     false },
    { "smsggetmessagesforaccount", &smsggetmessagesforaccount,            false,     false,     false },
#endif // ENABLE_WALLET
	{ "mintblock",              &mintblock,              false,     false,     false }
};

CRPCTable::CRPCTable()
{
    unsigned int vcidx;
    
	for (vcidx = 0; vcidx < (sizeof(vRPCCommands) / sizeof(vRPCCommands[0])); vcidx++)
    {
        const CRPCCommand *pcmd;

        pcmd = &vRPCCommands[vcidx];
        mapCommands[pcmd->name] = pcmd;
    }
}

const CRPCCommand *CRPCTable::operator[](std::string name) const
{
    std::map<std::string, const CRPCCommand*>::const_iterator it = mapCommands.find(name);
    
	if (it == mapCommands.end())
	{
        return NULL;
	}
	
    return (*it).second;
}

bool HTTPAuthorized(std::map<std::string, std::string>& mapHeaders)
{
    std::string strAuth = mapHeaders["authorization"];
	
    if (strAuth.substr(0,6) != "Basic ")
	{
        return false;
	}
	
    std::string strUserPass64 = strAuth.substr(6); boost::trim(strUserPass64);
    std::string strUserPass = DecodeBase64(strUserPass64);
    
	return TimingResistantEqual(strUserPass, strRPCUserColonPass);
}

void ErrorReply(std::ostream& stream, const json_spirit::Object& objError, const json_spirit::Value& id)
{
    // Send error reply from json-rpc error object
    int nStatus = HTTP_INTERNAL_SERVER_ERROR;
    int code = find_value(objError, "code").get_int();
    
	if (code == RPC_INVALID_REQUEST)
	{
		nStatus = HTTP_BAD_REQUEST;
	}
    else if (code == RPC_METHOD_NOT_FOUND)
	{
		nStatus = HTTP_NOT_FOUND;
	}
	
    std::string strReply = JSONRPCReply(json_spirit::Value::null, objError, id);
    
	stream << HTTPReply(nStatus, strReply, false) << std::flush;
}

bool ClientAllowed(const boost::asio::ip::address& address)
{
    // Make sure that IPv4-compatible and IPv4-mapped IPv6 addresses are treated as IPv4 addresses
    if (address.is_v6() &&
		(
			address.to_v6().is_v4_compatible() ||
			address.to_v6().is_v4_mapped()
		)
	)
	{
		return ClientAllowed(address.to_v6().to_v4());
	}
	
    if (address == boost::asio::ip::address_v4::loopback() ||
		address == boost::asio::ip::address_v6::loopback() ||
		(
			address.is_v4() &&
			// Check whether IPv4 addresses match 127.0.0.0/8 (loopback subnet)
			(address.to_v4().to_ulong() & 0xff000000) == 0x7f000000
		)
	)
	{
		return true;
	}
	
    const std::string strAddress = address.to_string();
    const std::vector<std::string>& vAllow = mapMultiArgs["-rpcallowip"];
    
	for(std::string strAllow : vAllow)
	{
        if (WildcardMatch(strAddress, strAllow))
		{
            return true;
		}
    }
	
	return false;
}

class AcceptedConnection
{
public:
    virtual ~AcceptedConnection() {}

    virtual std::iostream& stream() = 0;
    virtual std::string peer_address_to_string() const = 0;
    virtual void close() = 0;
};

template <typename Protocol>
class AcceptedConnectionImpl : public AcceptedConnection
{
public:
    AcceptedConnectionImpl(ioContext& io_context, boost::asio::ssl::context &context, bool fUseSSL)
			: sslStream(io_context, context), _d(sslStream, fUseSSL), _stream(_d)
    {
		
    }

    virtual std::iostream& stream()
    {
        return _stream;
    }

    virtual std::string peer_address_to_string() const
    {
        return peer.address().to_string();
    }

    virtual void close()
    {
        _stream.close();
    }

    typename Protocol::endpoint peer;
    boost::asio::ssl::stream<typename Protocol::socket> sslStream;

private:
    SSLIOStreamDevice<Protocol> _d;
    boost::iostreams::stream<SSLIOStreamDevice<Protocol>> _stream;
};

void ServiceConnection(AcceptedConnection *conn);

// Forward declaration required for RPCListen
template <typename Protocol>
static void RPCAcceptHandler(boost::shared_ptr< boost::asio::basic_socket_acceptor<Protocol> > acceptor, boost::asio::ssl::context& context,
		bool fUseSSL, AcceptedConnection* conn, const boost::system::error_code& error);

/**
 * Sets up I/O resources to accept and handle a new connection.
 */
template <typename Protocol>
static void RPCListen(boost::shared_ptr<boost::asio::basic_socket_acceptor<Protocol>> acceptor, boost::asio::ssl::context& context, const bool fUseSSL)
{
    AcceptedConnectionImpl<Protocol>* conn = new AcceptedConnectionImpl<Protocol>(GetIOServiceFromPtr(acceptor), context, fUseSSL);
    
	acceptor->async_accept(
		conn->sslStream.lowest_layer(),
		conn->peer,
		boost::bind(&RPCAcceptHandler<Protocol>, acceptor, boost::ref(context), fUseSSL, conn, boost::asio::placeholders::error)
	);
}

/**
 * Accept and handle incoming connection.
 */
template <typename Protocol>
static void RPCAcceptHandler(boost::shared_ptr<boost::asio::basic_socket_acceptor<Protocol>> acceptor, boost::asio::ssl::context& context,
		const bool fUseSSL, AcceptedConnection* conn, const boost::system::error_code& error)
{
    // Immediately start accepting new connections, except when we're cancelled or our socket is closed.
    if (error != boost::asio::error::operation_aborted && acceptor->is_open())
	{
        RPCListen(acceptor, context, fUseSSL);
	}
	
    AcceptedConnectionImpl<boost::asio::ip::tcp>* tcp_conn = dynamic_cast< AcceptedConnectionImpl<boost::asio::ip::tcp>* >(conn);

    // TODO: Actually handle errors
    if (error)
    {
        delete conn;
    }
    // Restrict callers by IP.  It is important to
    // do this before starting client thread, to filter out
    // certain DoS and misbehaving clients.
    else if (tcp_conn && !ClientAllowed(tcp_conn->peer.address()))
    {
        // Only send a 403 if we're not using SSL to prevent a DoS during the SSL handshake.
        if (!fUseSSL)
		{
            conn->stream() << HTTPReply(HTTP_FORBIDDEN, "", false) << std::flush;
        }
		
		delete conn;
    }
    else
	{
        ServiceConnection(conn);
        
		conn->close();
        
		delete conn;
    }
}

void StartRPCThreads()
{
    strRPCUserColonPass = mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"];
    
	if(
		(
			(mapArgs["-rpcpassword"] == "") ||
			(mapArgs["-rpcuser"] == mapArgs["-rpcpassword"])
		) &&
		Params().RequireRPCPassword()
	)
    {
        unsigned char rand_pwd[32];
        GetRandBytes(rand_pwd, 32);
        std::string strWhatAmI = "To use DigitalNoted";
        
		if (mapArgs.count("-server"))
		{
            strWhatAmI = strprintf(ui_translate("To use the %s option"), "\"-server\"");
		}
        else if (mapArgs.count("-daemon"))
		{
            strWhatAmI = strprintf(ui_translate("To use the %s option"), "\"-daemon\"");
        }
		
		uiInterface.ThreadSafeMessageBox(
			strprintf(
				ui_translate(
					"%s, you must set a rpcpassword in the configuration file:\n"
					"%s\n"
					"It is recommended you use the following random password:\n"
					"rpcuser=DigitalNoterpc\n"
					"rpcpassword=%s\n"
					"(you do not need to remember this password)\n"
					"The username and password MUST NOT be the same.\n"
					"If the file does not exist, create it with owner-readable-only file permissions.\n"
					"It is also recommended to set alertnotify so you are notified of problems;\n"
					"for example: alertnotify=echo %%s | mail -s \"DigitalNote Alert\" admin@foo.com\n"
				),
				strWhatAmI,
				GetConfigFile().string(),
				EncodeBase58(&rand_pwd[0],&rand_pwd[0]+32)
			),
			"",
			CClientUIInterface::MSG_ERROR
		);
        
		StartShutdown();
        
		return;
    }

    assert(rpc_io_service == NULL);
	
    rpc_io_service = new ioContext();
    rpc_ssl_context = new boost::asio::ssl::context(boost::asio::ssl::context::sslv23);

    const bool fUseSSL = GetBoolArg("-rpcssl", false);

    if (fUseSSL)
    {
        rpc_ssl_context->set_options(boost::asio::ssl::context::no_sslv2 | boost::asio::ssl::context::no_sslv3);

        boost::filesystem::path pathCertFile(GetArg("-rpcsslcertificatechainfile", "server.cert"));
        
		if (!pathCertFile.is_complete())
		{
			pathCertFile = boost::filesystem::path(GetDataDir()) / pathCertFile;
        }
		
		if (boost::filesystem::exists(pathCertFile))
		{
			rpc_ssl_context->use_certificate_chain_file(pathCertFile.string());
        }
		else
		{
			LogPrintf("ThreadRPCServer ERROR: missing server certificate file %s\n", pathCertFile.string());
		}
		
        boost::filesystem::path pathPKFile(GetArg("-rpcsslprivatekeyfile", "server.pem"));
        
		if (!pathPKFile.is_complete())
		{
			pathPKFile = boost::filesystem::path(GetDataDir()) / pathPKFile;
        }
		
		if (boost::filesystem::exists(pathPKFile))
		{
			rpc_ssl_context->use_private_key_file(pathPKFile.string(), boost::asio::ssl::context::pem);
		}
        else
		{
			LogPrintf("ThreadRPCServer ERROR: missing server private key file %s\n", pathPKFile.string());
		}
		
        std::string strCiphers = GetArg("-rpcsslciphers", "TLSv1.2+HIGH:TLSv1+HIGH:!SSLv3:!SSLv2:!aNULL:!eNULL:!3DES:@STRENGTH");
        SSL_CTX_set_cipher_list(rpc_ssl_context->native_handle(), strCiphers.c_str());
    }

    // Try a dual IPv6/IPv4 socket, falling back to separate IPv4 and IPv6 sockets
    const bool loopback = !mapArgs.count("-rpcallowip");
    boost::asio::ip::address bindAddress = loopback ? boost::asio::ip::address_v6::loopback() : boost::asio::ip::address_v6::any();
    boost::asio::ip::tcp::endpoint endpoint(bindAddress, GetArg("-rpcport", Params().RPCPort()));
    boost::system::error_code v6_only_error;
    boost::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor(new boost::asio::ip::tcp::acceptor(*rpc_io_service));

    bool fListening = false;
    std::string strerr;
	
    try
    {
        acceptor->open(endpoint.protocol());
        acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

        // Try making the socket dual IPv6/IPv4 (if listening on the "any" address)
        acceptor->set_option(boost::asio::ip::v6_only(loopback), v6_only_error);

        acceptor->bind(endpoint);
        acceptor->listen(boost::asio::socket_base::max_connections);

        RPCListen(acceptor, *rpc_ssl_context, fUseSSL);

        fListening = true;
    }
    catch(boost::system::system_error &e)
    {
        strerr = strprintf(ui_translate("An error occurred while setting up the RPC port %u for listening on IPv6, falling back to IPv4: %s"), endpoint.port(), e.what());
    }

    try
	{
        // If dual IPv6/IPv4 failed (or we're opening loopback interfaces only), open IPv4 separately
        if (!fListening || loopback || v6_only_error)
        {
            bindAddress = loopback ? boost::asio::ip::address_v4::loopback() : boost::asio::ip::address_v4::any();
            endpoint.address(bindAddress);

            acceptor.reset(new boost::asio::ip::tcp::acceptor(*rpc_io_service));
            acceptor->open(endpoint.protocol());
            acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor->bind(endpoint);
            acceptor->listen(boost::asio::socket_base::max_connections);

            RPCListen(acceptor, *rpc_ssl_context, fUseSSL);

            fListening = true;
        }
    }
    catch(boost::system::system_error &e)
    {
        strerr = strprintf(ui_translate("An error occurred while setting up the RPC port %u for listening on IPv4: %s"), endpoint.port(), e.what());
    }

    if (!fListening)
	{
        uiInterface.ThreadSafeMessageBox(strerr, "", CClientUIInterface::MSG_ERROR);
        
		StartShutdown();
        
		return;
    }

    rpc_worker_group = new boost::thread_group();
    
	for (int i = 0; i < GetArg("-rpcthreads", 4); i++)
	{
        rpc_worker_group->create_thread(boost::bind(&ioContext::run, rpc_io_service));
	}
}

void StopRPCThreads()
{
    if (rpc_io_service == NULL)
	{
		return;
	}
	
    deadlineTimers.clear();
    rpc_io_service->stop();
    
	if (rpc_worker_group != NULL)
	{
        rpc_worker_group->join_all();
    }
	
	delete rpc_worker_group;
	rpc_worker_group = NULL;
    
	delete rpc_ssl_context;
	rpc_ssl_context = NULL;
    
	delete rpc_io_service;
	rpc_io_service = NULL;
}

void RPCRunHandler(const boost::system::error_code& err, boost::function<void(void)> func)
{
    if (!err)
	{
        func();
	}
}

void RPCRunLater(const std::string& name, boost::function<void(void)> func, int64_t nSeconds)
{
    assert(rpc_io_service != NULL);

    if (deadlineTimers.count(name) == 0)
    {
        deadlineTimers.insert(
			std::make_pair(
				name,
				boost::shared_ptr<boost::asio::deadline_timer>(new boost::asio::deadline_timer(*rpc_io_service))
			)
		);
    }
	
    deadlineTimers[name]->expires_from_now(boost::posix_time::seconds(nSeconds));
    deadlineTimers[name]->async_wait(boost::bind(RPCRunHandler, _1, func));
}

class JSONRequest
{
public:
    json_spirit::Value id;
    std::string strMethod;
    json_spirit::Array params;
	
    JSONRequest();
    void parse(const json_spirit::Value& valRequest);
};

JSONRequest::JSONRequest()
{
	id = json_spirit::Value::null;
}

void JSONRequest::parse(const json_spirit::Value& valRequest)
{
    // Parse request
    if (valRequest.type() != json_spirit::obj_type)
	{
        throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid Request object");
    }
	
	const json_spirit::Object& request = valRequest.get_obj();

    // Parse id now so errors from here on will have the id
    id = find_value(request, "id");

    // Parse method
    json_spirit::Value valMethod = find_value(request, "method");
    
	if (valMethod.type() == json_spirit::null_type)
	{
        throw JSONRPCError(RPC_INVALID_REQUEST, "Missing method");
    }
	
	if (valMethod.type() != json_spirit::str_type)
	{
        throw JSONRPCError(RPC_INVALID_REQUEST, "Method must be a string");
    }
	
	strMethod = valMethod.get_str();
    
	if (strMethod != "getwork" && strMethod != "getblocktemplate")
	{
        LogPrint("rpc", "ThreadRPCServer method=%s\n", strMethod);
	}
	
    // Parse params
    json_spirit::Value valParams = find_value(request, "params");
    
	if (valParams.type() == json_spirit::array_type)
	{
        params = valParams.get_array();
	}
    else if (valParams.type() == json_spirit::null_type)
	{
        params = json_spirit::Array();
	}
    else
	{
        throw JSONRPCError(RPC_INVALID_REQUEST, "Params must be an array");
	}
}

static json_spirit::Object JSONRPCExecOne(const json_spirit::Value& req)
{
    json_spirit::Object rpc_result;
    JSONRequest jreq;
    
	try
	{
        jreq.parse(req);

        json_spirit::Value result = tableRPC.execute(jreq.strMethod, jreq.params);
        rpc_result = JSONRPCReplyObj(result, json_spirit::Value::null, jreq.id);
    }
    catch (json_spirit::Object& objError)
    {
        rpc_result = JSONRPCReplyObj(json_spirit::Value::null, objError, jreq.id);
    }
    catch (std::exception& e)
    {
        rpc_result = JSONRPCReplyObj(json_spirit::Value::null, JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
    }

    return rpc_result;
}

static std::string JSONRPCExecBatch(const json_spirit::Array& vReq)
{
    json_spirit::Array ret;
    
	for (unsigned int reqIdx = 0; reqIdx < vReq.size(); reqIdx++)
	{
        ret.push_back(JSONRPCExecOne(vReq[reqIdx]));
	}
	
    return write_string(json_spirit::Value(ret), false) + "\n";
}

void ServiceConnection(AcceptedConnection *conn)
{
    bool fRun = true;
    while (fRun)
    {
        int nProto = 0;
        std::map<std::string, std::string> mapHeaders;
        std::string strRequest, strMethod, strURI;

        // Read HTTP request line
        if (!ReadHTTPRequestLine(conn->stream(), nProto, strMethod, strURI))
		{
            break;
		}
		
        // Read HTTP message headers and body
        ReadHTTPMessage(conn->stream(), mapHeaders, strRequest, nProto, MAX_SIZE);

        if (strURI != "/") {
            conn->stream() << HTTPReply(HTTP_NOT_FOUND, "", false) << std::flush;
            
			break;
        }

        // Check authorization
        if (mapHeaders.count("authorization") == 0)
        {
            conn->stream() << HTTPReply(HTTP_UNAUTHORIZED, "", false) << std::flush;
            
			break;
        }
		
        if (!HTTPAuthorized(mapHeaders))
        {
            LogPrintf("ThreadRPCServer incorrect password attempt from %s\n", conn->peer_address_to_string());
            
			/**
				Deter brute-forcing short passwords.
				If this results in a DoS the user really
				shouldn't have their RPC port exposed.
			*/
            if (mapArgs["-rpcpassword"].size() < 20)
			{
                MilliSleep(250);
			}
			
            conn->stream() << HTTPReply(HTTP_UNAUTHORIZED, "", false) << std::flush;
            
			break;
        }
        
		if (mapHeaders["connection"] == "close")
		{
            fRun = false;
		}
		
        JSONRequest jreq;
        
		try
        {
            // Parse request
            json_spirit::Value valRequest;
            if (!read_string(strRequest, valRequest))
			{
                throw JSONRPCError(RPC_PARSE_ERROR, "Parse error");
			}
			
            std::string strReply;

            // singleton request
            if (valRequest.type() == json_spirit::obj_type)
			{
                jreq.parse(valRequest);

                json_spirit::Value result = tableRPC.execute(jreq.strMethod, jreq.params);

                // Send reply
                strReply = JSONRPCReply(result, json_spirit::Value::null, jreq.id);

            // array of requests
            }
			else if (valRequest.type() == json_spirit::array_type)
			{
                strReply = JSONRPCExecBatch(valRequest.get_array());
			}
            else
			{
                throw JSONRPCError(RPC_PARSE_ERROR, "Top-level object parse error");
			}
			
            conn->stream() << HTTPReply(HTTP_OK, strReply, fRun) << std::flush;
        }
        catch (json_spirit::Object& objError)
        {
            ErrorReply(conn->stream(), objError, jreq.id);
            
			break;
        }
        catch (std::exception& e)
        {
            ErrorReply(conn->stream(), JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
            
			break;
        }
    }
}

json_spirit::Value CRPCTable::execute(const std::string &strMethod, const json_spirit::Array &params) const
{
    // Find method
    const CRPCCommand *pcmd = tableRPC[strMethod];
    if (!pcmd)
	{
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found");
	}
	
#ifdef ENABLE_WALLET
    if (pcmd->reqWallet && !pwalletMain)
	{
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");
	}
#endif // ENABLE_WALLET

    // Observe safe mode
    std::string strWarning = GetWarnings("rpc");
    
	if (strWarning != "" &&
		!GetBoolArg("-disablesafemode", false) &&
        !pcmd->okSafeMode
	)
	{
        throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE, std::string("Safe mode: ") + strWarning);
	}
	
    try
    {
        // Execute
        json_spirit::Value result;
		
        {
            if (pcmd->threadSafe)
			{
                result = pcmd->actor(params, false);
			}
#ifdef ENABLE_WALLET
            else if (!pwalletMain)
			{
                LOCK(cs_main);
                
				result = pcmd->actor(params, false);
            }
			else
			{
                LOCK2(cs_main, pwalletMain->cs_wallet);
                
				result = pcmd->actor(params, false);
            }
#else // ENABLE_WALLET
            else
			{
                LOCK(cs_main);
                
				result = pcmd->actor(params, false);
            }
#endif // !ENABLE_WALLET
        }
		
        return result;
    }
    catch (std::exception& e)
    {
        throw JSONRPCError(RPC_MISC_ERROR, e.what());
    }
}

std::vector<std::string> CRPCTable::listCommands() const
{
    std::vector<std::string> commandList;
    typedef std::map<std::string, const CRPCCommand*> commandMap;

    std::transform(
		mapCommands.begin(),
		mapCommands.end(),
		std::back_inserter(commandList),
		boost::bind(&commandMap::value_type::first, _1)
	);
    
	return commandList;
}

std::string HelpExampleCli(const std::string &methodname, const std::string &args)
{
    return "> DigitalNoted " + methodname + " " + args + "\n";
}

std::string HelpExampleRpc(const std::string &methodname, const std::string &args)
{
    return "> curl --user myusername --data-binary '{\"jsonrpc\": \"1.0\", \"id\":\"curltest\", "
	"\"method\": \"" + methodname + "\", \"params\": [" + args + "] }' -H 'content-type: text/plain;' http://127.0.0.1:9998/\n";
}

const CRPCTable tableRPC;
