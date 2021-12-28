#include <cassert>

#include "serialize.h"
#include "cdatastream.h"

#include "ckeymetadata.h"

CKeyMetadata::CKeyMetadata()
{
	SetNull();
}

CKeyMetadata::CKeyMetadata(int64_t nCreateTime_)
{
	nVersion = CKeyMetadata::CURRENT_VERSION;
	nCreateTime = nCreateTime_;
}

unsigned int CKeyMetadata::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(nCreateTime);
	
	return nSerSize;
}

template<typename Stream>
void CKeyMetadata::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(nCreateTime);
}

template<typename Stream>
void CKeyMetadata::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(nCreateTime);
}

template void CKeyMetadata::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CKeyMetadata::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

void CKeyMetadata::SetNull()
{
	nVersion = CKeyMetadata::CURRENT_VERSION;
	nCreateTime = 0;
}

