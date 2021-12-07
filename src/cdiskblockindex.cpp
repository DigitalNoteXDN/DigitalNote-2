#include "compat.h"

#include "cblock.h"
#include "enums/serialize_type.h"
#include "main_extern.h"
#include "util.h"
#include "ctxout.h"
#include "ctxin.h"
#include "ctransaction.h"
#include "cdatastream.h"

#include "cdiskblockindex.h"

CDiskBlockIndex::CDiskBlockIndex()
{
	hashPrev = 0;
	hashNext = 0;
	blockHash = 0;
}

CDiskBlockIndex::CDiskBlockIndex(CBlockIndex* pindex) : CBlockIndex(*pindex)
{
	hashPrev = (pprev ? pprev->GetBlockHash() : 0);
	hashNext = (pnext ? pnext->GetBlockHash() : 0);
}

unsigned int CDiskBlockIndex::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	const bool fGetSize = true;
	const bool fWrite = false;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	s.nType = nType;
	s.nVersion = nVersion;
	
	if (!(nType & SER_GETHASH))
	{
		READWRITE(nVersion);
	}
	
	READWRITE(hashNext);
	READWRITE(nFile);
	READWRITE(nBlockPos);
	READWRITE(nHeight);
	READWRITE(nMint);
	READWRITE(nMoneySupply);
	READWRITE(nFlags);
	READWRITE(nStakeModifier);
	READWRITE(bnStakeModifierV2);
	
	if (IsProofOfStake())
	{
		READWRITE(prevoutStake);
		READWRITE(nStakeTime);
	}
	else if (fRead)
	{
		const_cast<CDiskBlockIndex*>(this)->prevoutStake.SetNull();
		const_cast<CDiskBlockIndex*>(this)->nStakeTime = 0;
	}
	
	READWRITE(hashProof);

	// block header
	READWRITE(this->nVersion);
	READWRITE(hashPrev);
	READWRITE(hashMerkleRoot);
	READWRITE(nTime);
	READWRITE(nBits);
	READWRITE(nNonce);
	READWRITE(blockHash);
	
	return nSerSize;
}

template<typename Stream>
void CDiskBlockIndex::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	if (!(nType & SER_GETHASH))
	{
		READWRITE(nVersion);
	}
	
	READWRITE(hashNext);
	READWRITE(nFile);
	READWRITE(nBlockPos);
	READWRITE(nHeight);
	READWRITE(nMint);
	READWRITE(nMoneySupply);
	READWRITE(nFlags);
	READWRITE(nStakeModifier);
	READWRITE(bnStakeModifierV2);
	
	if (IsProofOfStake())
	{
		READWRITE(prevoutStake);
		READWRITE(nStakeTime);
	}
	else if (fRead)
	{
		const_cast<CDiskBlockIndex*>(this)->prevoutStake.SetNull();
		const_cast<CDiskBlockIndex*>(this)->nStakeTime = 0;
	}
	
	READWRITE(hashProof);

	// block header
	READWRITE(this->nVersion);
	READWRITE(hashPrev);
	READWRITE(hashMerkleRoot);
	READWRITE(nTime);
	READWRITE(nBits);
	READWRITE(nNonce);
	READWRITE(blockHash);
}

template<typename Stream>
void CDiskBlockIndex::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	if (!(nType & SER_GETHASH))
	{
		READWRITE(nVersion);
	}
	
	READWRITE(hashNext);
	READWRITE(nFile);
	READWRITE(nBlockPos);
	READWRITE(nHeight);
	READWRITE(nMint);
	READWRITE(nMoneySupply);
	READWRITE(nFlags);
	READWRITE(nStakeModifier);
	READWRITE(bnStakeModifierV2);
	
	if (IsProofOfStake())
	{
		READWRITE(prevoutStake);
		READWRITE(nStakeTime);
	}
	else if (fRead)
	{
		const_cast<CDiskBlockIndex*>(this)->prevoutStake.SetNull();
		const_cast<CDiskBlockIndex*>(this)->nStakeTime = 0;
	}
	
	READWRITE(hashProof);

	// block header
	READWRITE(this->nVersion);
	READWRITE(hashPrev);
	READWRITE(hashMerkleRoot);
	READWRITE(nTime);
	READWRITE(nBits);
	READWRITE(nNonce);
	READWRITE(blockHash);
}

template void CDiskBlockIndex::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CDiskBlockIndex::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

uint256 CDiskBlockIndex::GetBlockHash() const
{
	if (fUseFastIndex && (nTime < GetAdjustedTime() - 24 * 60 * 60) && blockHash != 0)
	{
		return blockHash;
	}
	
	CBlock block;
	block.nVersion        = nVersion;
	block.hashPrevBlock   = hashPrev;
	block.hashMerkleRoot  = hashMerkleRoot;
	block.nTime           = nTime;
	block.nBits           = nBits;
	block.nNonce          = nNonce;

	const_cast<CDiskBlockIndex*>(this)->blockHash = block.GetHash();

	return blockHash;
}

std::string CDiskBlockIndex::ToString() const
{
	std::string str = "CDiskBlockIndex(";
	
	str += CBlockIndex::ToString();
	str += strprintf("\n                hashBlock=%s, hashPrev=%s, hashNext=%s)",
		GetBlockHash().ToString(),
		hashPrev.ToString(),
		hashNext.ToString());
		
	return str;
}

