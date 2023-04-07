#include "compat.h"

#include "util.h"
#include "serialize.h"
#include "hash.h"
#include "cdatastream.h"

#include "cwalletkey.h"

CWalletKey::CWalletKey(int64_t nExpires)
{
	nTimeCreated = (nExpires ? GetTime() : 0);
	nTimeExpires = nExpires;
}

unsigned int CWalletKey::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;
	
	s.nType = nType;
	s.nVersion = nVersion;
	
	if (!(nType & SER_GETHASH))
	{
		READWRITE(nVersion);
	}
	
	READWRITE(vchPrivKey);
	READWRITE(nTimeCreated);
	READWRITE(nTimeExpires);
	READWRITE(strComment);
	
	return nSerSize;
}

template<typename Stream>
void CWalletKey::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	if (!(nType & SER_GETHASH))
	{
		READWRITE(nVersion);
	}
	
	READWRITE(vchPrivKey);
	READWRITE(nTimeCreated);
	READWRITE(nTimeExpires);
	READWRITE(strComment);
}

template<typename Stream>
void CWalletKey::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	if (!(nType & SER_GETHASH))
	{
		READWRITE(nVersion);
	}
	
	READWRITE(vchPrivKey);
	READWRITE(nTimeCreated);
	READWRITE(nTimeExpires);
	READWRITE(strComment);
}

template void CWalletKey::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CWalletKey::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

