#include "chash160.h"

void CHash160::Finalize(unsigned char hash[OUTPUT_SIZE])
{
	unsigned char buf[sha.OUTPUT_SIZE];
	
	sha.Finalize(buf);
	CRIPEMD160().Write(buf, sha.OUTPUT_SIZE).Finalize(hash);
}

CHash160& CHash160::Write(const unsigned char *data, size_t len)
{
	sha.Write(data, len);
	
	return *this;
}

CHash160& CHash160::Reset()
{
	sha.Reset();
	
	return *this;
}

