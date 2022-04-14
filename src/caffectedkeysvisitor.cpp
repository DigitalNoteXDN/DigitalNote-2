#include "script.h"
#include "cscript.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"

#include "caffectedkeysvisitor.h"

CAffectedKeysVisitor::CAffectedKeysVisitor(const CKeyStore &keystoreIn, std::vector<CKeyID> &vKeysIn)
		: keystore(keystoreIn), vKeys(vKeysIn)
{
	
}

void CAffectedKeysVisitor::Process(const CScript &script)
{
	txnouttype type;
	std::vector<CTxDestination> vDest;
	int nRequired;
	
	if (ExtractDestinations(script, type, vDest, nRequired))
	{
		for(const CTxDestination &dest : vDest)
		{
			boost::apply_visitor(*this, dest);
		}
	}
}

void CAffectedKeysVisitor::operator()(const CKeyID &keyId)
{
	if (keystore.HaveKey(keyId))
	{
		vKeys.push_back(keyId);
	}
}

void CAffectedKeysVisitor::operator()(const CScriptID &scriptId)
{
	CScript script;
	
	if (keystore.GetCScript(scriptId, script))
	{
		Process(script);
	}
}

void CAffectedKeysVisitor::operator()(const CStealthAddress &stxAddr)
{
	CScript script;
}

void CAffectedKeysVisitor::operator()(const CNoDestination &none)
{
	
}

