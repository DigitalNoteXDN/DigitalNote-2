#include <cassert>

#include "serialize.h"
#include "cdatastream.h"

#include "smsg/address.h"

namespace DigitalNote {
namespace SMSG {

Address::Address()
{
	
}

Address::Address(std::string sAddr, bool receiveOn, bool receiveAnon)
{
	sAddress = sAddr;
	fReceiveEnabled = receiveOn;
	fReceiveAnon = receiveAnon;
}

unsigned int Address::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;
	
	s.nType = nType;
	s.nVersion = nVersion;
	
	READWRITE(this->sAddress);
	READWRITE(this->fReceiveEnabled);
	READWRITE(this->fReceiveAnon);
	
	return nSerSize;
}

template<typename Stream>
void Address::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(this->sAddress);
	READWRITE(this->fReceiveEnabled);
	READWRITE(this->fReceiveAnon);
}

template<typename Stream>
void Address::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(this->sAddress);
	READWRITE(this->fReceiveEnabled);
	READWRITE(this->fReceiveAnon);
}

template void Address::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void Address::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

} // namespace SMSG
} // namespace DigitalNote
