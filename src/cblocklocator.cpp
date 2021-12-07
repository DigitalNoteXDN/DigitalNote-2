#include <map>

#include "uint/uint256.h"
#include "cblockindex.h"
#include "chainparams.h"
#include "cchainparams.h"
#include "serialize.h"
#include "enums/serialize_type.h"
#include "main_extern.h"
#include "cdatastream.h"

#include "cblocklocator.h"

CBlockLocator::CBlockLocator()
{
	
}

CBlockLocator::CBlockLocator(const CBlockIndex* pindex)
{
	Set(pindex);
}

CBlockLocator::CBlockLocator(uint256 hashBlock)
{
	std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
	
	if (mi != mapBlockIndex.end())
	{
		Set((*mi).second);
	}
}

CBlockLocator::CBlockLocator(const std::vector<uint256>& vHaveIn)
{
	vHave = vHaveIn;
}

unsigned int CBlockLocator::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(vHave);
	
	return nSerSize;
}

template<typename Stream>
void CBlockLocator::Serialize(Stream& s, int nType, int nVersion) const
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
	
	READWRITE(vHave);
}

template<typename Stream>
void CBlockLocator::Unserialize(Stream& s, int nType, int nVersion)
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
	
	READWRITE(vHave);
}

template void CBlockLocator::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CBlockLocator::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

void CBlockLocator::SetNull()
{
	vHave.clear();
}

bool CBlockLocator::IsNull()
{
	return vHave.empty();
}

void CBlockLocator::Set(const CBlockIndex* pindex)
{
	int nStep = 1;
	
	vHave.clear();
	
	while (pindex)
	{
		vHave.push_back(pindex->GetBlockHash());

		// Exponentially larger steps back
		for (int i = 0; pindex && i < nStep; i++)
		{
			pindex = pindex->pprev;
		}
		
		if (vHave.size() > 10)
		{
			nStep *= 2;
		}
	}
	
	vHave.push_back(Params().HashGenesisBlock());
}

int CBlockLocator::GetDistanceBack()
{
	// Retrace how far back it was in the sender's branch
	int nDistance = 0;
	int nStep = 1;
	
	for(const uint256& hash : vHave)
	{
		std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hash);
		
		if (mi != mapBlockIndex.end())
		{
			CBlockIndex* pindex = (*mi).second;
			
			if (pindex->IsInMainChain())
			{
				return nDistance;
			}
		}
		
		nDistance += nStep;
		
		if (nDistance > 10)
		{
			nStep *= 2;
		}
	}
	
	return nDistance;
}

CBlockIndex* CBlockLocator::GetBlockIndex()
{
	// Find the first block the caller has in the main chain
	for(const uint256& hash : vHave)
	{
		std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hash);
		
		if (mi != mapBlockIndex.end())
		{
			CBlockIndex* pindex = (*mi).second;
			
			if (pindex->IsInMainChain())
			{
				return pindex;
			}
		}
	}
	
	return pindexGenesisBlock;
}

uint256 CBlockLocator::GetBlockHash()
{
	// Find the first block the caller has in the main chain
	for(const uint256& hash : vHave)
	{
		std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hash);
		
		if (mi != mapBlockIndex.end())
		{
			CBlockIndex* pindex = (*mi).second;
			
			if (pindex->IsInMainChain())
			{
				return hash;
			}
		}
	}
	
	return Params().HashGenesisBlock();
}

int CBlockLocator::GetHeight()
{
	CBlockIndex* pindex = GetBlockIndex();
	
	if (!pindex)
	{
		return 0;
	}
	
	return pindex->nHeight;
}

