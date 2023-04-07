#ifndef CBASICKEYSTORE_H
#define CBASICKEYSTORE_H

#include <map>
#include <set>

#include "ckeystore.h"

class CKey;
class CKeyID;
class CScript;
class CScriptID;

typedef std::map<CKeyID, CKey> KeyMap;
typedef std::map<CScriptID, CScript> ScriptMap;
typedef std::set<CScript> WatchOnlySet;

/** Basic key store, that keeps keys in an address->secret map */
class CBasicKeyStore : public CKeyStore
{
protected:
	KeyMap mapKeys;
	ScriptMap mapScripts;
	WatchOnlySet setWatchOnly;

public:
	bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey);
	bool HaveKey(const CKeyID &address) const;
	void GetKeys(std::set<CKeyID> &setAddress) const;
	bool GetKey(const CKeyID &address, CKey &keyOut) const;

	virtual bool AddCScript(const CScript& redeemScript);
	virtual bool HaveCScript(const CScriptID &hash) const;
	virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const;

	virtual bool AddWatchOnly(const CScript &dest);
	virtual bool RemoveWatchOnly(const CScript &dest);
	virtual bool HaveWatchOnly(const CScript &dest) const;
	virtual bool HaveWatchOnly() const;
};

#endif // CBASICKEYSTORE_H
