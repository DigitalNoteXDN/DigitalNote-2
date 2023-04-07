#ifndef CHASHWRITER_H
#define CHASHWRITER_H

#include <openssl/sha.h>

class uint256;

class CHashWriter
{
private:
	SHA256_CTX ctx;

public:
	int nType;
	int nVersion;

	CHashWriter(int nTypeIn, int nVersionIn);

	void Init();
	CHashWriter& write(const char *pch, size_t size);
	uint256 GetHash();

	template<typename T>
	CHashWriter& operator<<(const T& obj);
};

#endif // CHASHWRITER_H
