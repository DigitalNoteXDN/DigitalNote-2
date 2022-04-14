#ifndef CTRANSACTIONLOCK_H
#define CTRANSACTIONLOCK_H

#include <vector>

#include "uint/uint256.h"

class CConsensusVote;

class CTransactionLock
{
public:
	int nBlockHeight;
	uint256 txHash;
	std::vector<CConsensusVote> vecConsensusVotes;
	int nExpiration;
	int nTimeout;

	bool SignaturesValid();
	int CountSignatures();
	void AddSignature(CConsensusVote& cv);
	uint256 GetHash();
};

#endif // CTRANSACTIONLOCK_H
