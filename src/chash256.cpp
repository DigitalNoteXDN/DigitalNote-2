#include "chash256.h"

void CHash256::Finalize(unsigned char hash[OUTPUT_SIZE])
{
	unsigned char buf[sha.OUTPUT_SIZE];
	
	sha.Finalize(buf);
	sha.Reset().Write(buf, sha.OUTPUT_SIZE).Finalize(hash);
}

CHash256& CHash256::Write(const unsigned char *data, size_t len)
{
	sha.Write(data, len);
	
	return *this;
}

CHash256& CHash256::Reset()
{
	sha.Reset();
	
	return *this;
}

