#include "compat.h"

#include "txdb-leveldb.h"
#include "blockparams.h"
#include "spork.h"
#include "instantx.h"
#include "cblock.h"
#include "mining.h"
#include "ctransactionlock.h"
#include "main_extern.h"
#include "ctxmempool.h"
#include "main.h"
#include "ctxout.h"
#include "ctxin.h"
#include "thread.h"
#include "serialize.h"
#include "ctxindex.h"
#include "util.h"
#include "cblockindex.h"
#include "cdatastream.h"

#include "cmerkletx.h"

CMerkleTx::CMerkleTx()
{
	Init();
}

CMerkleTx::CMerkleTx(const CTransaction& txIn) : CTransaction(txIn)
{
	Init();
}

unsigned int CMerkleTx::GetSerializeSize(int nType, int nVersion) const
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
	
	nSerSize += SerReadWrite(s, *(CTransaction*)this, nType, nVersion, ser_action);
	nVersion = this->nVersion;
	
	READWRITE(hashBlock);
	READWRITE(vMerkleBranch);
	READWRITE(nIndex);
	
	return nSerSize;
}

template<typename Stream>
void CMerkleTx::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	nSerSize += SerReadWrite(s, *(CTransaction*)this, nType, nVersion, ser_action);
	nVersion = this->nVersion;
	
	READWRITE(hashBlock);
	READWRITE(vMerkleBranch);
	READWRITE(nIndex);
}

template<typename Stream>
void CMerkleTx::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	nSerSize += SerReadWrite(s, *(CTransaction*)this, nType, nVersion, ser_action);
	nVersion = this->nVersion;
	
	READWRITE(hashBlock);
	READWRITE(vMerkleBranch);
	READWRITE(nIndex);
}

template void CMerkleTx::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CMerkleTx::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

void CMerkleTx::Init()
{
	hashBlock = 0;
	nIndex = -1;
	fMerkleVerified = false;
}

int CMerkleTx::SetMerkleBranch(const CBlock* pblock)
{
	AssertLockHeld(cs_main);
	CBlock blockTmp;

	if (pblock == NULL)
	{
		// Load the block this tx is in
		CTxIndex txindex;
		
		if (!CTxDB("r").ReadTxIndex(GetHash(), txindex))
		{
			return 0;
		}
		
		if (!blockTmp.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos))
		{
			return 0;
			pblock = &blockTmp;
		}
	}

	if (pblock)
	{
		// Update the tx's hashBlock
		hashBlock = pblock->GetHash();

		// Locate the transaction
		for (nIndex = 0; nIndex < (int)pblock->vtx.size(); nIndex++)
		{
			if (pblock->vtx[nIndex] == *(CTransaction*)this)
			{
				break;
			}
		}
		
		if (nIndex == (int)pblock->vtx.size())
		{
			vMerkleBranch.clear();
			nIndex = -1;
			
			LogPrintf("ERROR: SetMerkleBranch() : couldn't find tx in block\n");
			
			return 0;
		}

		// Fill in merkle branch
		vMerkleBranch = pblock->GetMerkleBranch(nIndex);
	}

	// Is the tx in a block that's in the main chain
	std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);

	if (mi == mapBlockIndex.end())
	{
		return 0;
	}

	CBlockIndex* pindex = (*mi).second;

	if (!pindex || !pindex->IsInMainChain())
	{
		return 0;
	}

	return pindexBest->nHeight - pindex->nHeight + 1;
}

// Return depth of transaction in blockchain:
// -1  : not in blockchain, and not in memory pool (conflicted transaction)
//  0  : in memory pool, waiting to be included in a block
// >=1 : this many blocks deep in the main chain
int CMerkleTx::GetDepthInMainChain(CBlockIndex* &pindexRet, bool enableIX) const
{
	AssertLockHeld(cs_main);

	int nResult = GetDepthInMainChainINTERNAL(pindexRet);

	if (nResult == 0 && !mempool.exists(GetHash()))
	{
		return -1; // Not in chain, not in mempool
	}

	if(enableIX)
	{
		if (nResult < 10)
		{
			int signatures = GetTransactionLockSignatures();
			
			if(signatures >= INSTANTX_SIGNATURES_REQUIRED)
			{
				return nInstantXDepth+nResult;
			}
		}
	}

	return nResult;
}

int CMerkleTx::GetDepthInMainChain(bool enableIX) const
{
	CBlockIndex *pindexRet;

	return GetDepthInMainChain(pindexRet, enableIX);
}

int CMerkleTx::GetDepthInMainChainINTERNAL(CBlockIndex* &pindexRet) const
{
	if (hashBlock == 0 || nIndex == -1)
	{
		return 0;
	}

	AssertLockHeld(cs_main);

	// Find the block it claims to be in
	std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);

	if (mi == mapBlockIndex.end())
	{
		return 0;
	}

	CBlockIndex* pindex = (*mi).second;

	if (!pindex || !pindex->IsInMainChain())
	{
		return 0;
	}

	// Make sure the merkle branch connects to this block
	if (!fMerkleVerified)
	{
		if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
		{
			return 0;
		}
		
		fMerkleVerified = true;
	}

	pindexRet = pindex;

	return pindexBest->nHeight - pindex->nHeight + 1;
}

bool CMerkleTx::IsInMainChain() const
{
	CBlockIndex *pindexRet;

	return GetDepthInMainChainINTERNAL(pindexRet) > 0;
}

int CMerkleTx::GetBlocksToMaturity() const
{
	if (!(IsCoinBase() || IsCoinStake()))
	{
		return 0;
	}

	return std::max(0, nCoinbaseMaturity+75 - GetDepthInMainChain());
}

bool CMerkleTx::AcceptToMemoryPool(bool fLimitFree, bool fRejectInsaneFee, bool ignoreFees)
{
	return ::AcceptToMemoryPool(mempool, *this, fLimitFree, NULL, fRejectInsaneFee, ignoreFees);
}

int CMerkleTx::GetTransactionLockSignatures() const
{
	if(!IsSporkActive(SPORK_2_INSTANTX))
	{
		return -3;
	}

	if(!fEnableInstantX)
	{
		return -1;
	}

	//compile consessus vote
	std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(GetHash());

	if (i != mapTxLocks.end())
	{
		return (*i).second.CountSignatures();
	}

	return -1;
}

bool CMerkleTx::IsTransactionLockTimedOut() const
{
	if(!fEnableInstantX)
	{
		return -1;
	}

	//compile consessus vote
	std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(GetHash());

	if (i != mapTxLocks.end())
	{
		return GetTime() > (*i).second.nTimeout;
	}

	return false;
}

