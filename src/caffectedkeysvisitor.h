#ifndef CAFFECTEDKEYSVISITOR_H
#define CAFFECTEDKEYSVISITOR_H

#include <boost/variant/static_visitor.hpp>
#include <vector>

#include "ckeystore.h"

class CKeyID;
class CScript;
class CStealthAddress;
class CNoDestination;

class CAffectedKeysVisitor : public boost::static_visitor<void>
{
private:
    const CKeyStore &keystore;
    std::vector<CKeyID> &vKeys;

public:
    CAffectedKeysVisitor(const CKeyStore &keystoreIn, std::vector<CKeyID> &vKeysIn);

    void Process(const CScript &script);
    void operator()(const CKeyID &keyId);
    void operator()(const CScriptID &scriptId);
    void operator()(const CStealthAddress &stxAddr);
    void operator()(const CNoDestination &none);
};

#endif // CAFFECTEDKEYSVISITOR_H
