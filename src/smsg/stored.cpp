#include <cassert>

#include "serialize.h"
#include "cdatastream.h"

#include "smsg/stored.h"

namespace DigitalNote {
namespace SMSG {

unsigned int Stored::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(this->timeReceived);
	READWRITE(this->status);
	READWRITE(this->folderId);
	READWRITE(this->sAddrTo);
	READWRITE(this->sAddrOutbox);
	READWRITE(this->vchMessage);
	
	return nSerSize;
}

template<typename Stream>
void Stored::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(this->timeReceived);
	READWRITE(this->status);
	READWRITE(this->folderId);
	READWRITE(this->sAddrTo);
	READWRITE(this->sAddrOutbox);
	READWRITE(this->vchMessage);
}

template<typename Stream>
void Stored::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(this->timeReceived);
	READWRITE(this->status);
	READWRITE(this->folderId);
	READWRITE(this->sAddrTo);
	READWRITE(this->sAddrOutbox);
	READWRITE(this->vchMessage);
}

template void Stored::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void Stored::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

} // namespace SMSG
} // namespace DigitalNote
