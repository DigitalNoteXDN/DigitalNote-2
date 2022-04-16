#include <set>

#include "tinyformat.h"
#include "types/rpcfn_type.h"
#include "crpccommand.h"
#include "init.h"
#include "rpcserver.h"
#include "rpcvelocity.h"
#include "enums/rpcerrorcode.h"
#include "main.h"
#include "main_extern.h"
#include "util.h"
#include "thread.h"
#include "cwallet.h"
#include "rpcprotocol.h"
#include "boost_placeholders.h"

#include "crpctable.h"

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
	{ "mintblock",              &mintblock,              false,     false,     false },
	{ "debugrpcallowip",        &debugrpcallowip,        false,     false,     false }
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

const CRPCCommand* CRPCTable::operator[](std::string name) const
{
	mapCommands_t::const_iterator it = mapCommands.find(name);

	if (it == mapCommands.end())
	{
		return NULL;
	}

	return (*it).second;
}

std::string CRPCTable::help(const std::string &strCommand) const
{
	std::string strRet;
	std::set<rpcfn_type> setDone;

	for (mapCommands_t::const_iterator mi = mapCommands.begin(); mi != mapCommands.end(); ++mi)
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

	strRet = strRet.substr(0, strRet.size() - 1);

	return strRet;
}

json_spirit::Value CRPCTable::execute(const std::string &strMethod, const json_spirit::Array &params) const
{
	// Find method
	const CRPCCommand* pcmd = tableRPC[strMethod];

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

	std::transform(
		mapCommands.begin(),
		mapCommands.end(),
		std::back_inserter(commandList),
		boost::bind(
			&mapCommands_t::value_type::first,
			boost::placeholders::_1
		)
	);

	return commandList;
}

