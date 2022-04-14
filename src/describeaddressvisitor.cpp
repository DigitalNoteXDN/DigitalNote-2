#include "cpubkey.h"
#include "init.h"
#include "cwallet.h"
#include "util.h"
#include "cscript.h"
#include "enums/txnouttype.h"
#include "cnodestination.h"
#include "cstealthaddress.h"
#include "cscriptid.h"
#include "ckeyid.h"
#include "script.h"
#include "cdigitalnoteaddress.h"

#include "describeaddressvisitor.h"

DescribeAddressVisitor::DescribeAddressVisitor(isminetype mineIn) : mine(mineIn)
{
	
}

json_spirit::Object DescribeAddressVisitor::operator()(const CNoDestination &dest) const
{
	return json_spirit::Object();
}

json_spirit::Object DescribeAddressVisitor::operator()(const CKeyID &keyID) const
{
	json_spirit::Object obj;
	CPubKey vchPubKey;
	
	obj.push_back(json_spirit::Pair("isscript", false));
	
	if (mine == ISMINE_SPENDABLE)
	{
		pwalletMain->GetPubKey(keyID, vchPubKey);
		
		obj.push_back(json_spirit::Pair("pubkey", HexStr(vchPubKey)));
		obj.push_back(json_spirit::Pair("iscompressed", vchPubKey.IsCompressed()));
	}
	
	return obj;
}

json_spirit::Object DescribeAddressVisitor::operator()(const CScriptID &scriptID) const
{
	json_spirit::Object obj;
	obj.push_back(json_spirit::Pair("isscript", true));
	
	if (mine != ISMINE_NO)
	{
		CScript subscript;
		
		pwalletMain->GetCScript(scriptID, subscript);
		
		std::vector<CTxDestination> addresses;
		txnouttype whichType;
		int nRequired;
		
		ExtractDestinations(subscript, whichType, addresses, nRequired);
		
		obj.push_back(json_spirit::Pair("script", GetTxnOutputType(whichType)));
		obj.push_back(json_spirit::Pair("hex", HexStr(subscript.begin(), subscript.end())));
		
		json_spirit::Array a;
		
		for(const CTxDestination& addr : addresses)
		{
			a.push_back(CDigitalNoteAddress(addr).ToString());
		}
		
		obj.push_back(json_spirit::Pair("addresses", a));
		
		if (whichType == TX_MULTISIG)
		{
			obj.push_back(json_spirit::Pair("sigsrequired", nRequired));
		}
	}
	
	return obj;
}

json_spirit::Object DescribeAddressVisitor::operator()(const CStealthAddress &stxAddr) const
{
	json_spirit::Object obj;
	
	obj.push_back(json_spirit::Pair("todo", true));
	
	return obj;
}

