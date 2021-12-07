#include "cscript.h"

#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"

#include "cscriptvisitor.h"

CScriptVisitor::CScriptVisitor(CScript *scriptin)
{
	script = scriptin;
}

bool CScriptVisitor::operator()(const CNoDestination &dest) const
{
	script->clear();
	
	return false;
}

bool CScriptVisitor::operator()(const CKeyID &keyID) const
{
	script->clear();
	
	*script << OP_DUP << OP_HASH160 << keyID << OP_EQUALVERIFY << OP_CHECKSIG;
	
	return true;
}

bool CScriptVisitor::operator()(const CScriptID &scriptID) const
{
	script->clear();
	
	*script << OP_HASH160 << scriptID << OP_EQUAL;
	
	return true;
}

bool CScriptVisitor::operator()(const CStealthAddress &stxAddr) const
{
	script->clear();
	
	//*script << OP_HASH160 << scriptID << OP_EQUAL;
	
	printf("TODO\n");
	
	return false;
}

