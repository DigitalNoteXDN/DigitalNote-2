#include "tinyformat.h"
#include "serialize.h"
#include "cdatastream.h"
#include "cvarint.h"

#include "cdiskblockpos.h"

CDiskBlockPos::CDiskBlockPos()
{
	SetNull();
}

CDiskBlockPos::CDiskBlockPos(int nFileIn, unsigned int nPosIn)
{
	nFile = nFileIn;
	nPos = nPosIn;
}

unsigned int CDiskBlockPos::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(VARINT(nFile));
    READWRITE(VARINT(nPos));
	
	return nSerSize;
}

template<typename Stream>
void CDiskBlockPos::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(VARINT(nFile));
    READWRITE(VARINT(nPos));
}

template<typename Stream>
void CDiskBlockPos::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(VARINT(nFile));
    READWRITE(VARINT(nPos));
}

template void CDiskBlockPos::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CDiskBlockPos::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

bool operator==(const CDiskBlockPos &a, const CDiskBlockPos &b)
{
	return (a.nFile == b.nFile && a.nPos == b.nPos);
}

bool operator!=(const CDiskBlockPos &a, const CDiskBlockPos &b)
{
	return !(a == b);
}

void CDiskBlockPos::SetNull()
{
	nFile = -1;
	nPos = 0;
}

bool CDiskBlockPos::IsNull() const
{
	return (nFile == -1);
}

std::string CDiskBlockPos::ToString() const
{
	return strprintf("CBlockDiskPos(nFile=%i, nPos=%i)", nFile, nPos);
}

