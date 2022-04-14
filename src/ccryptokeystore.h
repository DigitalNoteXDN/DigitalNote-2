#ifndef CCRYPTOKEYSTORE_H
#define CCRYPTOKEYSTORE_H

#include <set>
#include <boost/signals2/signal.hpp>

#include "cbasickeystore.h"
#include "types/cryptedkeymap.h"
#include "crypter.h"

class CKey;
class CKeyID;
class CPubKey;

/** Keystore which keeps the private keys encrypted.
 * It derives from the basic key store, which is used if no encryption is active.
 */
class CCryptoKeyStore : public CBasicKeyStore
{
private:
	// if fUseCrypto is true, mapKeys must be empty
	// if fUseCrypto is false, vMasterKey must be empty
	bool fUseCrypto;

protected:
	CryptedKeyMap mapCryptedKeys;
	CKeyingMaterial vMasterKey;

	bool SetCrypted();

	// will encrypt previously unencrypted keys
	bool EncryptKeys(CKeyingMaterial& vMasterKeyIn);

	bool Unlock(const CKeyingMaterial& vMasterKeyIn);

public:
	CCryptoKeyStore();

	bool IsCrypted() const;
	bool IsLocked() const;
	bool LockKeyStore();

	virtual bool AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);
	bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey);
	bool HaveKey(const CKeyID &address) const;
	bool GetKey(const CKeyID &address, CKey& keyOut) const;
	bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const;
	void GetKeys(std::set<CKeyID> &setAddress) const;

	/* Wallet status (encrypted, locked) changed.
	 * Note: Called without locks held.
	 */
	boost::signals2::signal<void (CCryptoKeyStore* wallet)> NotifyStatusChanged;
};

#endif // CCRYPTOKEYSTORE_H
