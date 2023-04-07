#ifndef CTXMEMPOOL_H
#define CTXMEMPOOL_H

#include "types/ccriticalsection.h"

class CInPoint;
class COutPoint;
class CTransaction;
class uint256;

/*
 * CTxMemPool stores valid-according-to-the-current-best-chain
 * transactions that may be included in the next block.
 *
 * Transactions are added when they are seen on the network
 * (or created by the local node), but not all transactions seen
 * are added to the pool: if a new transaction double-spends
 * an input of a transaction in the pool, it is dropped,
 * as are non-standard transactions.
 */
class CTxMemPool
{
private:
	unsigned int nTransactionsUpdated;

public:
	mutable CCriticalSection cs;

	std::map<uint256, CTransaction> mapTx;
	std::map<COutPoint, CInPoint> mapNextTx;

	CTxMemPool();

	bool addUnchecked(const uint256& hash, CTransaction &tx);
	bool remove(const CTransaction &tx, bool fRecursive = false);
	bool removeConflicts(const CTransaction &tx);
	void clear();
	void queryHashes(std::vector<uint256>& vtxid);
	unsigned int GetTransactionsUpdated() const;
	void AddTransactionsUpdated(unsigned int n);
	unsigned long size() const;
	bool exists(uint256 hash) const;
	bool lookup(uint256 hash, CTransaction& result) const;
};

#endif // CTXMEMPOOL_H
