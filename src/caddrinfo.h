#ifndef CADDRESINFO_H
#define CADDRESINFO_H

#include "caddress.h"
#include "util.h"

/** Extended statistics about a CAddress */
class CAddrInfo : public CAddress
{
private:
	CNetAddr source;			// where knowledge about this address first came from
	int64_t nLastSuccess;		// last successful connection by us
	// int64_t CAddress::nLastTry		// last try whatsoever by us:
	int nAttempts;				// connection attempts since last successful attempt
	int nRefCount;				// reference count in new sets (memory only)
	bool fInTried;				// in tried set? (memory only)
	int nRandomPos;				// position in vRandom

	friend class CAddrMan;

public:
	CAddrInfo(const CAddress &addrIn, const CNetAddr &addrSource);
	CAddrInfo();

	void Init();

	// Calculate in which "tried" bucket this entry belongs
	int GetTriedBucket(const std::vector<unsigned char> &nKey) const;

	// Calculate in which "new" bucket this entry belongs, given a certain source
	int GetNewBucket(const std::vector<unsigned char> &nKey, const CNetAddr& src) const;

	// Calculate in which "new" bucket this entry belongs, using its default source
	int GetNewBucket(const std::vector<unsigned char> &nKey) const;

	// Determine whether the statistics about this entry are bad enough so that it can just be deleted
	bool IsTerrible(int64_t nNow = GetAdjustedTime()) const;

	// Calculate the relative chance this entry should be given when selecting nodes to connect to
	double GetChance(int64_t nNow = GetAdjustedTime()) const;

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CADDRESINFO_H
