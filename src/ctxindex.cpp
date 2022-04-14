#include "cblock.h"
#include "cblockindex.h"
#include "serialize.h"
#include "enums/serialize_type.h"
#include "main_extern.h"
#include "chashwriter.h"
#include "ctxout.h"
#include "ctxin.h"
#include "ctransaction.h"
#include "cautofile.h"
#include "cdatastream.h"

#include "ctxindex.h"

CTxIndex::CTxIndex()
{
	SetNull();
}

CTxIndex::CTxIndex(const CDiskTxPos& posIn, unsigned int nOutputs)
{
	pos = posIn;
	vSpent.resize(nOutputs);
}

unsigned int CTxIndex::GetSerializeSize(int nType, int nVersion) const
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

	READWRITE(pos);
	READWRITE(vSpent);

	return nSerSize;
}

template<typename Stream>
void CTxIndex::Serialize(Stream& s, int nType, int nVersion) const
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

	READWRITE(pos);
	READWRITE(vSpent);
}

template<typename Stream>
void CTxIndex::Unserialize(Stream& s, int nType, int nVersion)
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

	READWRITE(pos);
	READWRITE(vSpent);
}

template void CTxIndex::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CTxIndex::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void CTxIndex::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void CTxIndex::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
template void CTxIndex::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;

void CTxIndex::SetNull()
{
	pos.SetNull();
	vSpent.clear();
}

bool CTxIndex::IsNull()
{
	return pos.IsNull();
}

bool operator==(const CTxIndex& a, const CTxIndex& b)
{
	return (a.pos == b.pos &&
			a.vSpent == b.vSpent);
}

bool operator!=(const CTxIndex& a, const CTxIndex& b)
{
	return !(a == b);
}

int CTxIndex::GetDepthInMainChain() const
{
	// Read block header
	CBlock block;

	if (!block.ReadFromDisk(pos.nFile, pos.nBlockPos, false))
	{
		return 0;
	}

	// Find the block in the index
	std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(block.GetHash());

	if (mi == mapBlockIndex.end())
	{
		return 0;
	}

	CBlockIndex* pindex = (*mi).second;

	if (!pindex || !pindex->IsInMainChain())
	{
		return 0;
	}

	return 1 + nBestHeight - pindex->nHeight;
}
