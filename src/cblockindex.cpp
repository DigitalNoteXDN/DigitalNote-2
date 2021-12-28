#include "compat.h"

#include "cblock.h"
#include "ctransaction.h"
#include "cdiskblockpos.h"
#include "cbignum.h"
#include "main_extern.h"
#include "main_const.h"
#include "util.h"
#include "ctxout.h"
#include "ctxin.h"
#include "serialize.h"

#include "cblockindex.h"

CBlockIndex::CBlockIndex()
{
	phashBlock = NULL;
	pprev = NULL;
	pnext = NULL;
	nFile = 0;
	nBlockPos = 0;
	nHeight = 0;
	nChainTrust = 0;
	nMint = 0;
	nMoneySupply = 0;
	nFlags = 0;
	nStakeModifier = 0;
	bnStakeModifierV2 = 0;
	hashProof = 0;
	prevoutStake.SetNull();
	nStakeTime = 0;
	nSequenceId = 0;

	nVersion       = 0;
	hashMerkleRoot = 0;
	nTime          = 0;
	nBits          = 0;
	nNonce         = 0;
}

CBlockIndex::CBlockIndex(unsigned int nFileIn, unsigned int nBlockPosIn, CBlock& block)
{
	phashBlock = NULL;
	pprev = NULL;
	pnext = NULL;
	nFile = nFileIn;
	nBlockPos = nBlockPosIn;
	nHeight = 0;
	nChainTrust = 0;
	nMint = 0;
	nMoneySupply = 0;
	nFlags = 0;
	nStakeModifier = 0;
	bnStakeModifierV2 = 0;
	hashProof = 0;
	nSequenceId = 0;
	
	if (block.IsProofOfStake())
	{
		SetProofOfStake();
		prevoutStake = block.vtx[1].vin[0].prevout;
		nStakeTime = block.vtx[1].nTime;
	}
	else
	{
		prevoutStake.SetNull();
		nStakeTime = 0;
	}

	nVersion       = block.nVersion;
	hashMerkleRoot = block.hashMerkleRoot;
	nTime          = block.nTime;
	nBits          = block.nBits;
	nNonce         = block.nNonce;
}

CBlock CBlockIndex::GetBlockHeader() const
{
	CBlock block;
	
	block.nVersion       = nVersion;
	block.hashMerkleRoot = hashMerkleRoot;
	block.nTime          = nTime;
	block.nBits          = nBits;
	block.nNonce         = nNonce;
	
	if (pprev)
	{
		block.hashPrevBlock = pprev->GetBlockHash();
	}
	
	return block;
}

CDiskBlockPos CBlockIndex::GetBlockPos() const
{
	CDiskBlockPos ret;
	
	ret.nFile = nFile;
	ret.nPos  = nBlockPos;
	
	return ret;
}

uint256 CBlockIndex::GetBlockHash() const
{
	return *phashBlock;
}

int64_t CBlockIndex::GetBlockTime() const
{
	return (int64_t)nTime;
}

/* Calculates trust score for a block given */
uint256 CBlockIndex::GetBlockTrust() const
{
	CBigNum bnTarget;

	bnTarget.SetCompact(nBits);

	if (bnTarget <= 0)
	{
		return 0;
	}

	return ((CBigNum(1) << 256) / (bnTarget + 1)).getuint256();
}

bool CBlockIndex::IsInMainChain() const
{
	return (pnext || this == pindexBest);
}

bool CBlockIndex::CheckIndex() const
{
	return true;
}

int64_t CBlockIndex::GetPastTimeLimit() const
{
	return GetBlockTime() - nDrift;
}

int64_t CBlockIndex::GetMedianTimePast() const
{
	int64_t pmedian[nMedianTimeSpan];
	int64_t* pbegin = &pmedian[nMedianTimeSpan];
	int64_t* pend = &pmedian[nMedianTimeSpan];

	const CBlockIndex* pindex = this;
	
	for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
	{
		*(--pbegin) = pindex->GetBlockTime();
	}
	
	std::sort(pbegin, pend);
	
	return pbegin[(pend - pbegin) / 2];
}

bool CBlockIndex::IsProofOfWork() const
{
	return !(nFlags & BLOCK_PROOF_OF_STAKE);
}

bool CBlockIndex::IsProofOfStake() const
{
	return (nFlags & BLOCK_PROOF_OF_STAKE);
}

void CBlockIndex::SetProofOfStake()
{
	nFlags |= BLOCK_PROOF_OF_STAKE;
}

unsigned int CBlockIndex::GetStakeEntropyBit() const
{
	return ((nFlags & BLOCK_STAKE_ENTROPY) >> 1);
}

bool CBlockIndex::SetStakeEntropyBit(unsigned int nEntropyBit)
{
	if (nEntropyBit > 1)
	{
		return false;
	}

	nFlags |= (nEntropyBit? BLOCK_STAKE_ENTROPY : 0);
	
	return true;
}

bool CBlockIndex::GeneratedStakeModifier() const
{
	return (nFlags & BLOCK_STAKE_MODIFIER);
}

void CBlockIndex::SetStakeModifier(uint64_t nModifier, bool fGeneratedStakeModifier)
{
	nStakeModifier = nModifier;
	
	if (fGeneratedStakeModifier)
	{
		nFlags |= BLOCK_STAKE_MODIFIER;
	}
}

std::string CBlockIndex::ToString() const
{
	return strprintf("CBlockIndex(nprev=%p, pnext=%p, nFile=%u, nBlockPos=%-6d nHeight=%d, nMint=%s, nMoneySupply=%s, nFlags=(%s)(%d)(%s), nStakeModifier=%016x, hashProof=%s, prevoutStake=(%s), nStakeTime=%d merkle=%s, hashBlock=%s)",
		pprev, pnext, nFile, nBlockPos, nHeight,
		FormatMoney(nMint), FormatMoney(nMoneySupply),
		GeneratedStakeModifier() ? "MOD" : "-", GetStakeEntropyBit(), IsProofOfStake()? "PoS" : "PoW",
		nStakeModifier,
		hashProof.ToString(),
		prevoutStake.ToString(), nStakeTime,
		hashMerkleRoot.ToString(),
		GetBlockHash().ToString()
	);
}

