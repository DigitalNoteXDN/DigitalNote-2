#ifndef CADDRMAN_H
#define CADDRMAN_H

#include <vector>
#include <map>
#include <set>

#include "types/ccriticalsection.h"
#include "util.h"

class CNetAddr;
class CAddrInfo;
class CAddress;
class CService;

/** Stochastical (IP) address manager */
class CAddrMan
{
private:
	mutable CCriticalSection cs;		// critical section to protect the inner data structures

	std::vector<unsigned char> nKey;			// secret key to randomize bucket select with
	int nIdCount;								// last used nId
	std::map<int, CAddrInfo> mapInfo;			// table with information about all nIds
	std::map<CNetAddr, int> mapAddr;			// find an nId based on its network address
	std::vector<int> vRandom;					// randomly-ordered vector of all nIds
	int nTried;									// number of "tried" entries
	std::vector<std::vector<int> > vvTried;		// list of "tried" buckets
	int nNew;									// number of (unique) "new" entries
	std::vector<std::set<int> > vvNew;			// list of "new" buckets

protected:
	// Find an entry.
	CAddrInfo* Find(const CNetAddr& addr, int *pnId = NULL);

	// find an entry, creating it if necessary.
	// nTime and nServices of found node is updated, if necessary.
	CAddrInfo* Create(const CAddress &addr, const CNetAddr &addrSource, int *pnId = NULL);

	// Swap two elements in vRandom.
	void SwapRandom(unsigned int nRandomPos1, unsigned int nRandomPos2);

	// Return position in given bucket to replace.
	int SelectTried(int nKBucket);

	// Remove an element from a "new" bucket.
	// This is the only place where actual deletes occur.
	// They are never deleted while in the "tried" table, only possibly evicted back to the "new" table.
	int ShrinkNew(int nUBucket);

	// Move an entry from the "new" table(s) to the "tried" table
	// @pre vvUnkown[nOrigin].count(nId) != 0
	void MakeTried(CAddrInfo& info, int nId, int nOrigin);

	// Mark an entry "good", possibly moving it from "new" to "tried".
	void Good_(const CService &addr, int64_t nTime);

	// Add an entry to the "new" table.
	bool Add_(const CAddress &addr, const CNetAddr& source, int64_t nTimePenalty);

	// Mark an entry as attempted to connect.
	void Attempt_(const CService &addr, int64_t nTime);

	// Select an address to connect to.
	// nUnkBias determines how much to favor new addresses over tried ones (min=0, max=100)
	CAddress Select_(int nUnkBias);

#ifdef DEBUG_ADDRMAN
	// Perform consistency check. Returns an error code or zero.
	int Check_();
#endif

	// Select several addresses at once.
	void GetAddr_(std::vector<CAddress> &vAddr);

	// Mark an entry as currently-connected-to.
	void Connected_(const CService &addr, int64_t nTime);

public:
	CAddrMan();

	// Return the number of (unique) addresses in all tables.
	int size();

	// Consistency check
	void Check();

	// Add a single address.
	bool Add(const CAddress &addr, const CNetAddr& source, int64_t nTimePenalty = 0);

	// Add multiple addresses.
	bool Add(const std::vector<CAddress> &vAddr, const CNetAddr& source, int64_t nTimePenalty = 0);

	// Mark an entry as accessible.
	void Good(const CService &addr, int64_t nTime = GetAdjustedTime());

	// Mark an entry as connection attempted to.
	void Attempt(const CService &addr, int64_t nTime = GetAdjustedTime());

	// Choose an address to connect to.
	// nUnkBias determines how much "new" entries are favored over "tried" ones (0-100).
	CAddress Select(int nUnkBias = 50);

	// Return a bunch of addresses, selected at random.
	std::vector<CAddress> GetAddr();

	// Mark an entry as currently-connected-to.
	void Connected(const CService &addr, int64_t nTime = GetAdjustedTime());

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CADDRMAN_H
