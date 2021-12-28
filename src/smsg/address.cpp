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
	sAddress            = sAddr;
	fReceiveEnabled     = receiveOn;
	fReceiveAnon        = receiveAnon;
}

unsigned int Address::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(this->sAddress);
	READWRITE(this->fReceiveEnabled);
	READWRITE(this->fReceiveAnon);
	
	return nSerSize;
}

template<typename Stream>
void Address::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(this->sAddress);
	READWRITE(this->fReceiveEnabled);
	READWRITE(this->fReceiveAnon);
}

template<typename Stream>
void Address::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(this->sAddress);
	READWRITE(this->fReceiveEnabled);
	READWRITE(this->fReceiveAnon);
}

template void Address::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void Address::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

} // namespace SMSG
} // namespace DigitalNote
