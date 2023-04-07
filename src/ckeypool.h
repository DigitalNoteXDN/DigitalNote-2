#ifndef CKEYPOOL_H
#define CKEYPOOL_H

#include <cstdint>

#include "cpubkey.h"

/** A key pool entry */
class CKeyPool
{
public:
	int64_t nTime;
	CPubKey vchPubKey;

	CKeyPool();
	CKeyPool(const CPubKey& vchPubKeyIn);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CKEYPOOL_H
