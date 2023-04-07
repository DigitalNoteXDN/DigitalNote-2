#include "compat.h"

#include "uint/uint256.h"
#include "util.h"
#include "hash.h"
#include "csizecomputer.h"
#include "cdatastream.h"
#include "serialize.h"

#include "csporkmessage.h"

uint256 CSporkMessage::GetHash()
{
	uint256 n = Hash(BEGIN(nSporkID), END(nTimeSigned));
	
	return n;
}

size_t CSporkMessage::GetSerializeSize(int nType, int nVersion) const
{
	CSizeComputer s(nType, nVersion);
	
	NCONST_PTR(this)->SerializationOp(s, CSerActionSerialize(), nType, nVersion);
	
	return s.size();
}

template<typename Stream>
void CSporkMessage::Serialize(Stream& s, int nType, int nVersion) const
{
	NCONST_PTR(this)->SerializationOp(s, CSerActionSerialize(), nType, nVersion);
}

template void CSporkMessage::Serialize<CDataStream>(CDataStream&, int, int) const;

template<typename Stream>
void CSporkMessage::Unserialize(Stream& s, int nType, int nVersion)
{
	SerializationOp(s, CSerActionUnserialize(), nType, nVersion);
}

template void CSporkMessage::Unserialize<CDataStream>(CDataStream&, int, int);

template <typename Stream, typename Operation>
inline void CSporkMessage::SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
{
	unsigned int nSerSize = 0;
	
	READWRITE(nSporkID);
	READWRITE(nValue);
	READWRITE(nTimeSigned);
	READWRITE(vchSig);
}

