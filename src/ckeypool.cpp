#include "compat.h"

#include "util.h"
#include "enums/serialize_type.h"
#include "cdatastream.h"

#include "ckeypool.h"

CKeyPool::CKeyPool()
{
	nTime = GetTime();
}

CKeyPool::CKeyPool(const CPubKey& vchPubKeyIn)
{
	nTime = GetTime();
	vchPubKey = vchPubKeyIn;
}

unsigned int CKeyPool::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(nTime);
	READWRITE(vchPubKey);
	
	return nSerSize;
}

template<typename Stream>
void CKeyPool::Serialize(Stream& s, int nType, int nVersion) const
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
	
	READWRITE(nTime);
	READWRITE(vchPubKey);
}

template<typename Stream>
void CKeyPool::Unserialize(Stream& s, int nType, int nVersion)
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
	
	READWRITE(nTime);
	READWRITE(vchPubKey);
}

template void CKeyPool::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CKeyPool::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

