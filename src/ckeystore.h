#ifndef CKEYSTORE_H
#define CKEYSTORE_H

#include <set>

#include "types/ccriticalsection.h"

class CKey;
class CPubKey;
class CKeyID;
class CScript;
class CScriptID;

/** A virtual base class for key stores */
class CKeyStore
{
protected:
	mutable CCriticalSection cs_KeyStore;

public:
	virtual ~CKeyStore();

	// Add a key to the store.
	virtual bool AddKeyPubKey(const CKey &key, const CPubKey &pubkey) =0;
	virtual bool AddKey(const CKey &key);

	// Check whether a key corresponding to a given address is present in the store.
	virtual bool HaveKey(const CKeyID &address) const =0;
	virtual bool GetKey(const CKeyID &address, CKey& keyOut) const =0;
	virtual void GetKeys(std::set<CKeyID> &setAddress) const =0;
	virtual bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const;

	// Support for BIP 0013 : see https://en.bitcoin.it/wiki/BIP_0013
	virtual bool AddCScript(const CScript& redeemScript) =0;
	virtual bool HaveCScript(const CScriptID &hash) const =0;
	virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const =0;

	//! Support for Watch-only addresses
	virtual bool AddWatchOnly(const CScript &dest) =0;
	virtual bool RemoveWatchOnly(const CScript &dest) =0;
	virtual bool HaveWatchOnly(const CScript &dest) const =0;
	virtual bool HaveWatchOnly() const =0;
};

#endif // CKEYSTORE_H
