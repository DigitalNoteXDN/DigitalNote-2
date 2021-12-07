#include "tinyformat.h"
#include "serialize.h"
#include "chashwriter.h"
#include "cautofile.h"
#include "cdatastream.h"

#include "cdisktxpos.h"

CDiskTxPos::CDiskTxPos()
{
	this->SetNull();
}

CDiskTxPos::CDiskTxPos(unsigned int nFileIn, unsigned int nBlockPosIn, unsigned int nTxPosIn)
		: nFile(nFileIn), nBlockPos(nBlockPosIn), nTxPos(nTxPosIn)
{
	
};

unsigned int CDiskTxPos::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(FLATDATA(*this));
	
	return nSerSize;
}

template<typename Stream>
void CDiskTxPos::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(FLATDATA(*this));
}

template<typename Stream>
void CDiskTxPos::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(FLATDATA(*this));
}

template void CDiskTxPos::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CDiskTxPos::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void CDiskTxPos::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void CDiskTxPos::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
template void CDiskTxPos::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;

void CDiskTxPos::SetNull()
{
	this->nFile = (unsigned int) -1;
	this->nBlockPos = 0;
	this->nTxPos = 0;
}

bool CDiskTxPos::IsNull() const
{
	return (this->nFile == (unsigned int) -1);
}

bool operator==(const CDiskTxPos& a, const CDiskTxPos& b)
{
	return (a.nFile     == b.nFile &&
			a.nBlockPos == b.nBlockPos &&
			a.nTxPos    == b.nTxPos);
}

bool operator!=(const CDiskTxPos& a, const CDiskTxPos& b)
{
	return !(a == b);
}
	
std::string CDiskTxPos::ToString() const
{
	if (IsNull())
	{
		return "null";
	}
	else
	{
		return strprintf("(nFile=%u, nBlockPos=%u, nTxPos=%u)", nFile, nBlockPos, nTxPos);
	}
}

