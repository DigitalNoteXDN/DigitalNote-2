#include <cassert>

#include "serialize.h"
#include "cdatastream.h"

#include "cstealthkeymetadata.h"

CStealthKeyMetadata::CStealthKeyMetadata()
{
	
}

CStealthKeyMetadata::CStealthKeyMetadata(CPubKey pkEphem_, CPubKey pkScan_)
{
	pkEphem = pkEphem_;
	pkScan = pkScan_;
}

unsigned int CStealthKeyMetadata::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(pkEphem);
	READWRITE(pkScan);
	
	return nSerSize;
}

template<typename Stream>
void CStealthKeyMetadata::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(pkEphem);
	READWRITE(pkScan);
}

template<typename Stream>
void CStealthKeyMetadata::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(pkEphem);
	READWRITE(pkScan);
}

template void CStealthKeyMetadata::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CStealthKeyMetadata::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
//template void CStealthKeyMetadata::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
//template void CStealthKeyMetadata::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
//template void CStealthKeyMetadata::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;

