#include "serialize.h"
#include "enums/serialize_type.h"
#include "cdatastream.h"

#include "caccount.h"

CAccount::CAccount()
{
	SetNull();
}

unsigned int CAccount::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(vchPubKey);
	
	return nSerSize;
}

template<typename Stream>
void CAccount::Serialize(Stream& s, int nType, int nVersion) const
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
	
	READWRITE(vchPubKey);
}

template<typename Stream>
void CAccount::Unserialize(Stream& s, int nType, int nVersion)
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
	
	READWRITE(vchPubKey);
}

template void CAccount::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CAccount::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

void CAccount::SetNull()
{
	vchPubKey = CPubKey();
}

