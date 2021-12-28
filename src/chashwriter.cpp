#include "uint/uint256.h"
#include "serialize.h"
#include "ctransaction.h"
#include "ctxout.h"
#include "cflatdata.h"
#include "cvarint.h"

#include "chashwriter.h"

CHashWriter::CHashWriter(int nTypeIn, int nVersionIn) : nType(nTypeIn), nVersion(nVersionIn)
{
	Init();
}

void CHashWriter::Init()
{
	SHA256_Init(&ctx);
}

CHashWriter& CHashWriter::write(const char *pch, size_t size)
{
	SHA256_Update(&ctx, pch, size);
	
	return (*this);
}

// invalidates the object
uint256 CHashWriter::GetHash()
{
	uint256 hash1;
	uint256 hash2;
	
	SHA256_Final((unsigned char*)&hash1, &ctx);
	
	SHA256((unsigned char*)&hash1, sizeof(hash1), (unsigned char*)&hash2);
	
	return hash2;
}

template<typename T>
CHashWriter& CHashWriter::operator<<(const T& obj)
{
	// Serialize to this stream
	::Serialize(*this, obj, nType, nVersion);
	
	return (*this);
}

template CHashWriter& CHashWriter::operator<< <int>(int const&);
template CHashWriter& CHashWriter::operator<< <std::string>(std::string const&);
template CHashWriter& CHashWriter::operator<< <CTransaction>(CTransaction const&);
template CHashWriter& CHashWriter::operator<< <CFlatData>(CFlatData const&);
template CHashWriter& CHashWriter::operator<< <CVarInt<unsigned int> >(CVarInt<unsigned int> const&);
template CHashWriter& CHashWriter::operator<< <CTxOut>(CTxOut const&);

