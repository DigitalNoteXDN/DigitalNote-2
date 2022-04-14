#include "tinyformat.h"
#include "serialize.h"
#include "chashwriter.h"
#include "cautofile.h"
#include "cdatastream.h"
#include "cflatdata.h"

#include "coutpoint.h"

COutPoint::COutPoint()
{
	SetNull();
}

COutPoint::COutPoint(uint256 hashIn, unsigned int nIn)
{
	hash = hashIn;
	n = nIn;
}

unsigned int COutPoint::GetSerializeSize(int nType, int nVersion) const
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
void COutPoint::Serialize(Stream& s, int nType, int nVersion) const
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
void COutPoint::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(FLATDATA(*this));
}

template void COutPoint::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void COutPoint::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void COutPoint::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void COutPoint::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
template void COutPoint::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;

void COutPoint::SetNull()
{
	hash = 0;
	n = (unsigned int) - 1;
}

bool COutPoint::IsNull() const
{
	return (hash == 0 && n == (unsigned int) -1);
}

bool operator<(const COutPoint& a, const COutPoint& b)
{
	return (a.hash < b.hash || (a.hash == b.hash && a.n < b.n));
}

bool operator==(const COutPoint& a, const COutPoint& b)
{
	return (a.hash == b.hash && a.n == b.n);
}

bool operator!=(const COutPoint& a, const COutPoint& b)
{
	return !(a == b);
}

std::string COutPoint::ToString() const
{
	return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0,10), n);
}

std::string COutPoint::ToStringShort() const
{
	return strprintf("%s-%u", hash.ToString().substr(0,64), n);
}

