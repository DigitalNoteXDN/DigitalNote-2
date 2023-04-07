#include "compat.h"

#include <stdint.h>

#include "net/proxytype.h"
#include "net/cservice.h"
#include "net.h"
#include "netbase.h"
#include "util.h"
#include "version.h"
#include "init.h"
#include "rpcserver.h"
#include "main_extern.h"
#include "cblockindex.h"
#include "chainparams.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "cstealthaddress.h"
#include "cscriptid.h"
#include "ckeyid.h"
#include "script.h"
#include "enums/rpcerrorcode.h"
#include "chashwriter.h"
#include "enums/serialize_type.h"
#include "spork.h"
#include "csporkmessage.h"
#include "rpcprotocol.h"

#ifdef ENABLE_WALLET
#include "walletdb.h"
#include "cwallet.h"
#include "wallet.h"

#include "describeaddressvisitor.h"
#endif

json_spirit::Value getinfo(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error(
			"getinfo\n"
			"Returns an object containing various state info."
		);
	}

	proxyType proxy;
	GetProxy(NET_IPV4, proxy);

	json_spirit::Object obj, diff;
	obj.push_back(json_spirit::Pair("version", FormatFullVersion()));
	obj.push_back(json_spirit::Pair("protocolversion",(int)PROTOCOL_VERSION));

#ifdef ENABLE_WALLET

	if (pwalletMain)
	{
		obj.push_back(json_spirit::Pair("walletversion", pwalletMain->GetVersion()));
		obj.push_back(json_spirit::Pair("balance", ValueFromAmount(pwalletMain->GetBalance())));
		obj.push_back(json_spirit::Pair("newmint", ValueFromAmount(pwalletMain->GetNewMint())));
		obj.push_back(json_spirit::Pair("stake", ValueFromAmount(pwalletMain->GetStake())));
	}

#endif // ENABLE_WALLET

	obj.push_back(json_spirit::Pair("blocks", (int)nBestHeight));
	obj.push_back(json_spirit::Pair("timeoffset", (int64_t)GetTimeOffset()));
	obj.push_back(json_spirit::Pair("moneysupply", ValueFromAmount(pindexBest->nMoneySupply)));
	obj.push_back(json_spirit::Pair("connections", (int)vNodes.size()));
	obj.push_back(json_spirit::Pair("proxy", (proxy.first.IsValid() ? proxy.first.ToStringIPPort() : std::string())));
	obj.push_back(json_spirit::Pair("ip", GetLocalAddress(NULL).ToStringIP()));

	diff.push_back(json_spirit::Pair("proof-of-work", GetDifficulty()));
	diff.push_back(json_spirit::Pair("proof-of-stake", GetDifficulty(GetLastBlockIndex(pindexBest, true))));

	obj.push_back(json_spirit::Pair("difficulty", diff));
	obj.push_back(json_spirit::Pair("testnet", TestNet()));

#ifdef ENABLE_WALLET

	if (pwalletMain)
	{
		obj.push_back(json_spirit::Pair("keypoololdest", (int64_t)pwalletMain->GetOldestKeyPoolTime()));
		obj.push_back(json_spirit::Pair("keypoolsize", (int)pwalletMain->GetKeyPoolSize()));
	}

	obj.push_back(json_spirit::Pair("paytxfee", ValueFromAmount(nTransactionFee)));
	obj.push_back(json_spirit::Pair("mininput", ValueFromAmount(nMinimumInputValue)));

	if (pwalletMain && pwalletMain->IsCrypted())
	{
		obj.push_back(json_spirit::Pair("unlocked_until", (int64_t)nWalletUnlockTime));
	}
	
#endif // ENABLE_WALLET

	obj.push_back(json_spirit::Pair("errors", GetWarnings("statusbar")));

	return obj;
}

json_spirit::Value validateaddress(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"validateaddress <DigitalNote>\n"
			"Return information about <DigitalNote>."
		);
	}

	CDigitalNoteAddress address(params[0].get_str());
	bool isValid = address.IsValid();

	json_spirit::Object ret;
	ret.push_back(json_spirit::Pair("isvalid", isValid));

	if (isValid)
	{
		CTxDestination dest = address.Get();
		std::string currentAddress = address.ToString();
		
		ret.push_back(json_spirit::Pair("address", currentAddress));
#ifdef ENABLE_WALLET
		
		isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
		
		ret.push_back(json_spirit::Pair("ismine", (mine & ISMINE_SPENDABLE) ? true : false));
		
		if (mine != ISMINE_NO) {
			ret.push_back(json_spirit::Pair("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true: false));
			
			json_spirit::Object detail = boost::apply_visitor(DescribeAddressVisitor(mine), dest);
			
			ret.insert(ret.end(), detail.begin(), detail.end());
		}
		
		if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
		{
			ret.push_back(json_spirit::Pair("account", pwalletMain->mapAddressBook[dest]));
		}
#endif // ENABLE_WALLET
	}

	return ret;
}

json_spirit::Value validatepubkey(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || !params.size() || params.size() > 2)
	{
		throw std::runtime_error(
			"validatepubkey <DigitalNotepubkey>\n"
			"Return information about <DigitalNotepubkey>."
		);
	}

	std::vector<unsigned char> vchPubKey = ParseHex(params[0].get_str());
	CPubKey pubKey(vchPubKey);

	bool isValid = pubKey.IsValid();
	bool isCompressed = pubKey.IsCompressed();
	CKeyID keyID = pubKey.GetID();

	CDigitalNoteAddress address;
	address.Set(keyID);

	json_spirit::Object ret;
	ret.push_back(json_spirit::Pair("isvalid", isValid));

	if (isValid)
	{
		CTxDestination dest = address.Get();
		std::string currentAddress = address.ToString();
		
		ret.push_back(json_spirit::Pair("address", currentAddress));
		ret.push_back(json_spirit::Pair("iscompressed", isCompressed));
#ifdef ENABLE_WALLET
		
		isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
		
		ret.push_back(json_spirit::Pair("ismine", (mine & ISMINE_SPENDABLE) ? true : false));
		
		if (mine != ISMINE_NO)
		{
			ret.push_back(json_spirit::Pair("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true: false));
			
			json_spirit::Object detail = boost::apply_visitor(DescribeAddressVisitor(mine), dest);
			
			ret.insert(ret.end(), detail.begin(), detail.end());
		}
		
		if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
		{
			ret.push_back(json_spirit::Pair("account", pwalletMain->mapAddressBook[dest]));
		}
#endif // ENABLE_WALLET
	}

	return ret;
}

json_spirit::Value verifymessage(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 3)
	{
		throw std::runtime_error(
			"verifymessage <DigitalNote> <signature> <message>\n"
			"Verify a signed message"
		);
	}

	std::string strAddress = params[0].get_str();
	std::string strSign = params[1].get_str();
	std::string strMessage = params[2].get_str();

	CDigitalNoteAddress addr(strAddress);
	if (!addr.IsValid())
	{
		throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
	}

	CKeyID keyID;
	if (!addr.GetKeyID(keyID))
	{
		throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
	}

	bool fInvalid = false;
	std::vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

	if (fInvalid)
	{
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");
	}

	CHashWriter ss(SER_GETHASH, 0);
	ss << strMessageMagic;
	ss << strMessage;

	CPubKey pubkey;
	if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
	{
		return false;
	}

	return (pubkey.GetID() == keyID);
}

/*
	Used for updating/reading spork settings on the network
*/
json_spirit::Value spork(const json_spirit::Array& params, bool fHelp)
{
	if(params.size() == 1 && params[0].get_str() == "show")
	{
		std::map<int, CSporkMessage>::iterator it = mapSporksActive.begin();

		json_spirit::Object ret;
		while(it != mapSporksActive.end())
		{
			ret.push_back(json_spirit::Pair(sporkManager.GetSporkNameByID(it->second.nSporkID), it->second.nValue));
			it++;
		}
		
		return ret;
	}
	else if (params.size() == 2)
	{
		int nSporkID = sporkManager.GetSporkIDByName(params[0].get_str());
		if(nSporkID == -1)
		{
			return "Invalid spork name";
		}

		// SPORK VALUE
		int64_t nValue = params[1].get_int();

		//broadcast new spork
		if(sporkManager.UpdateSpork(nSporkID, nValue))
		{
			return "success";
		}
		else
		{
			return "failure";
		}

	}

	throw std::runtime_error(
		"spork <name> [<value>]\n"
		"<name> is the corresponding spork name, or 'show' to show all current spork settings"
		"<value> is a epoch datetime to enable or disable spork"
		+ HelpRequiringPassphrase()
	);
}

