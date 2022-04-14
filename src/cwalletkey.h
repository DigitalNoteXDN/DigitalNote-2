#ifndef CWALLETKEY_H
#define CWALLETKEY_H

#include <cstdint>
#include <string>

#include "types/cprivkey.h"

/** Private key that includes an expiration date in case it never gets used. */
class CWalletKey
{
public:
	CPrivKey vchPrivKey;
	int64_t nTimeCreated;
	int64_t nTimeExpires;
	std::string strComment;
	//// todo: add something to note what created it (user, getnewaddress, change)
	////   maybe should have a std::map<std::string, std::string> property map

	CWalletKey(int64_t nExpires=0);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CWALLETKEY_H
