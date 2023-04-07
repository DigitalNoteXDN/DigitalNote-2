#ifndef CACCOUNT_H
#define CACCOUNT_H

#include "cpubkey.h"

/** Account information.
 * Stored in wallet with key "acc"+string account name.
 */
class CAccount
{
public:
	CPubKey vchPubKey;

	CAccount();
	void SetNull();

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CACCOUNT_H
