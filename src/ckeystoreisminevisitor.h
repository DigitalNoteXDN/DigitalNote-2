#ifndef CKEYSTOREISMINEVISITOR_H
#define CKEYSTOREISMINEVISITOR_H

#include <boost/variant/static_visitor.hpp>

class CKeyStore;
class CNoDestination;
class CKeyID;
class CScriptID;
class CStealthAddress;

class CKeyStoreIsMineVisitor : public boost::static_visitor<bool>
{
private:
	const CKeyStore *keystore;

public:
	CKeyStoreIsMineVisitor(const CKeyStore *keystoreIn);

	bool operator()(const CNoDestination &dest) const;
	bool operator()(const CKeyID &keyID) const;
	bool operator()(const CScriptID &scriptID) const;
	bool operator()(const CStealthAddress &stxAddr) const;
};

#endif // CKEYSTOREISMINEVISITOR_H
