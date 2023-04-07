#include "compat.h"

#include <cassert>

#include "serialize.h"
#include "enums/serialize_type.h"
#include "version.h"
#include "cdatastream.h"

#include "caddress.h"

CAddress::CAddress() : CService()
{
	Init();
}

CAddress::CAddress(CService ipIn, uint64_t nServicesIn) : CService(ipIn)
{
	Init();
	nServices = nServicesIn;
}

void CAddress::Init()
{
	nServices = NODE_NETWORK;
	nTime = 100000000;
	nLastTry = 0;
}

unsigned int CAddress::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;

	s.nType = nType;
	s.nVersion = nVersion;
	
	CAddress* pthis = const_cast<CAddress*>(this);
	CService* pip = (CService*)pthis;
	
	if (nType & SER_DISK)
	{
		READWRITE(nVersion);
	}
	
	if ((nType & SER_DISK) || (nVersion >= CADDR_TIME_VERSION && !(nType & SER_GETHASH)))
	{
		READWRITE(nTime);
	}
	
	READWRITE(nServices);
	READWRITE(*pip);
	
	return nSerSize;
}

template<typename Stream>
void CAddress::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	CAddress* pthis = const_cast<CAddress*>(this);
	CService* pip = (CService*)pthis;
	
	if (nType & SER_DISK)
	{
		READWRITE(nVersion);
	}
	
	if ((nType & SER_DISK) || (nVersion >= CADDR_TIME_VERSION && !(nType & SER_GETHASH)))
	{
		READWRITE(nTime);
	}
	
	READWRITE(nServices);
	READWRITE(*pip);
}

template<typename Stream>
void CAddress::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	CAddress* pthis = const_cast<CAddress*>(this);
	CService* pip = (CService*)pthis;
	
	pthis->Init();
	
	if (nType & SER_DISK)
	{
		READWRITE(nVersion);
	}
	
	if ((nType & SER_DISK) || (nVersion >= CADDR_TIME_VERSION && !(nType & SER_GETHASH)))
	{
		READWRITE(nTime);
	}
	
	READWRITE(nServices);
	READWRITE(*pip);
}

template void CAddress::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CAddress::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

