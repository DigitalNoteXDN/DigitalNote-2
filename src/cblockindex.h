#ifndef CBLOCKINDEX_H
#define CBLOCKINDEX_H

#include "uint/uint256.h"
#include "coutpoint.h"

class CBlock;
class CBlockIndex;
class CDiskBlockPos;

/** The block chain is a tree shaped structure starting with the
 * genesis block at the root, with each block potentially having multiple
 * candidates to be the next block.  pprev and pnext link a path through the
 * main/longest chain.  A blockindex may have multiple pprev pointing back
 * to it, but pnext will only point forward to the longest branch, or will
 * be null if the block is not part of the longest chain.
 */
class CBlockIndex
{
public:
	const uint256* phashBlock;
	CBlockIndex* pprev;
	CBlockIndex* pnext;
	unsigned int nFile;
	unsigned int nBlockPos;
	uint256 nChainTrust; // ppcoin: trust score of block chain
	int nHeight;

	int64_t nMint;
	int64_t nMoneySupply;

	unsigned int nFlags;  // ppcoin: block index flags
	enum
	{
		BLOCK_PROOF_OF_STAKE = (1 << 0), // is proof-of-stake block
		BLOCK_STAKE_ENTROPY  = (1 << 1), // entropy bit for stake modifier
		BLOCK_STAKE_MODIFIER = (1 << 2), // regenerated stake modifier
	};

	enum
	{
		nMedianTimeSpan = 11
	};

	uint64_t nStakeModifier; // hash modifier for proof-of-stake
	uint256 bnStakeModifierV2;

	// proof-of-stake specific fields
	COutPoint prevoutStake;
	unsigned int nStakeTime;

	uint256 hashProof;

	// block header
	int nVersion;
	uint256 hashMerkleRoot;
	unsigned int nTime;
	unsigned int nBits;
	unsigned int nNonce;
	// (memory only) Sequencial id assigned to distinguish order in which blocks are received.
	uint32_t nSequenceId;

	CBlockIndex();
	CBlockIndex(unsigned int nFileIn, unsigned int nBlockPosIn, CBlock& block);

	CBlock GetBlockHeader() const;
	CDiskBlockPos GetBlockPos() const;
	uint256 GetBlockHash() const;
	int64_t GetBlockTime() const;
	uint256 GetBlockTrust() const;
	bool IsInMainChain() const;
	bool CheckIndex() const;
	int64_t GetPastTimeLimit() const;
	int64_t GetMedianTimePast() const;
	bool IsProofOfWork() const;
	bool IsProofOfStake() const;
	void SetProofOfStake();
	unsigned int GetStakeEntropyBit() const;
	bool SetStakeEntropyBit(unsigned int nEntropyBit);
	bool GeneratedStakeModifier() const;
	void SetStakeModifier(uint64_t nModifier, bool fGeneratedStakeModifier);
	std::string ToString() const;
};

#endif // CBLOCKINDEX_H
