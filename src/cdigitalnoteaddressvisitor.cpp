#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"

#include "cdigitalnoteaddressvisitor.h"

CDigitalNoteAddressVisitor::CDigitalNoteAddressVisitor(CDigitalNoteAddress* addrIn) : addr(addrIn)
{
	
}

bool CDigitalNoteAddressVisitor::operator()(const CKeyID &id) const
{
	return addr->Set(id);
}

bool CDigitalNoteAddressVisitor::operator()(const CScriptID &id) const
{
	return addr->Set(id);
}

bool CDigitalNoteAddressVisitor::operator()(const CNoDestination &no) const
{
	return false;
}

bool CDigitalNoteAddressVisitor::operator()(const CStealthAddress &stxAddr) const
{
	return false;
}

