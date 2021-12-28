#include "cdatastream.h"
#include "cautofile.h"
#include "chashwriter.h"

#include "cflatdata.h"

CFlatData::CFlatData(void* pbeginIn, void* pendIn) : pbegin((char*)pbeginIn), pend((char*)pendIn)
{
	
}

char* CFlatData::begin()
{
	return pbegin;
}

const char* CFlatData::begin() const
{
	return pbegin;
}

char* CFlatData::end()
{
	return pend;
}

const char* CFlatData::end() const
{
	return pend;
}

unsigned int CFlatData::GetSerializeSize(int, int) const
{
	return pend - pbegin;
}

template<typename Stream>
void CFlatData::Serialize(Stream& s, int, int) const
{
	s.write(pbegin, pend - pbegin);
}

template void CFlatData::Serialize<CDataStream>(CDataStream&, int, int) const;
template void CFlatData::Serialize<CAutoFile>(CAutoFile&, int, int) const;
template void CFlatData::Serialize<CHashWriter>(CHashWriter&, int, int) const;

template<typename Stream>
void CFlatData::Unserialize(Stream& s, int, int)
{
	s.read(pbegin, pend - pbegin);
}

template void CFlatData::Unserialize<CDataStream>(CDataStream&, int, int);
template void CFlatData::Unserialize<CAutoFile>(CAutoFile&, int, int);