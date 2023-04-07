#ifndef CSCRIPTVISITOR_H
#define CSCRIPTVISITOR_H

#include <boost/variant/static_visitor.hpp>

class CScript;
class CNoDestination;
class CKeyID;
class CScriptID;
class CStealthAddress;

class CScriptVisitor : public boost::static_visitor<bool>
{
private:
	CScript* script;

public:
	CScriptVisitor(CScript *scriptin);

	bool operator()(const CNoDestination &dest) const;
	bool operator()(const CKeyID &keyID) const;
	bool operator()(const CScriptID &scriptID) const;
	bool operator()(const CStealthAddress &stxAddr) const;
};

#endif // CSCRIPTVISITOR_H
