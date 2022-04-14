#include <openssl/ripemd.h>

#include "ctransaction.h"
#include "ctxout.h"
#include "chashwriter.h"
#include "uint/uint160.h"
#include "uint/uint256.h"
#include "types/ec_point.h"
#include "serialize.h"
#include "cdatastream.h"

#include "hash.h"

template<typename T1>
uint256 Hash(const T1 pbegin, const T1 pend)
{
	static unsigned char pblank[1];
	uint256 hash1;
	uint256 hash2;

	SHA256(
		(pbegin == pend ? pblank : (unsigned char*)&pbegin[0]),
		(pend - pbegin) * sizeof(pbegin[0]),
		(unsigned char*)&hash1
	);

	SHA256(
		(unsigned char*)&hash1,
		sizeof(hash1),
		(unsigned char*)&hash2
	);

	return hash2;
}

template<typename T1, typename T2>
uint256 Hash(const T1 p1begin, const T1 p1end, const T2 p2begin, const T2 p2end)
{
	static unsigned char pblank[1];
	uint256 hash1;
	SHA256_CTX ctx;

	SHA256_Init(&ctx);

	SHA256_Update(&ctx, (p1begin == p1end ? pblank : (unsigned char*)&p1begin[0]), (p1end - p1begin) * sizeof(p1begin[0]));
	SHA256_Update(&ctx, (p2begin == p2end ? pblank : (unsigned char*)&p2begin[0]), (p2end - p2begin) * sizeof(p2begin[0]));

	SHA256_Final((unsigned char*)&hash1, &ctx);

	uint256 hash2;

	SHA256(
		(unsigned char*)&hash1,
		sizeof(hash1),
		(unsigned char*)&hash2
	);

	return hash2;
}

template<typename T1, typename T2, typename T3>
uint256 Hash(const T1 p1begin, const T1 p1end, const T2 p2begin, const T2 p2end, const T3 p3begin, const T3 p3end)
{
	static unsigned char pblank[1];
	uint256 hash1;
	SHA256_CTX ctx;
	uint256 hash2;

	SHA256_Init(&ctx);

	SHA256_Update(&ctx, (p1begin == p1end ? pblank : (unsigned char*)&p1begin[0]), (p1end - p1begin) * sizeof(p1begin[0]));
	SHA256_Update(&ctx, (p2begin == p2end ? pblank : (unsigned char*)&p2begin[0]), (p2end - p2begin) * sizeof(p2begin[0]));
	SHA256_Update(&ctx, (p3begin == p3end ? pblank : (unsigned char*)&p3begin[0]), (p3end - p3begin) * sizeof(p3begin[0]));

	SHA256_Final((unsigned char*)&hash1, &ctx);

	SHA256(
		(unsigned char*)&hash1,
		sizeof(hash1),
		(unsigned char*)&hash2
	);

	return hash2;
}

template uint256 Hash<char*>(char*, char*);
template uint256 Hash<char*, char*>(char*, char*, char*, char*);
template uint256 Hash<CDataStream::iterator>(CDataStream::iterator, CDataStream::iterator);
template uint256 Hash<ec_point::iterator>(ec_point::iterator, ec_point::iterator);
template uint256 Hash<std::string::const_iterator>(std::string::const_iterator, std::string::const_iterator);

template<typename T>
uint256 SerializeHash(const T& obj, int nType, int nVersion)
{
	CHashWriter ss(nType, nVersion);

	ss << obj;

	return ss.GetHash();
}

template uint256 SerializeHash<CTxOut>(CTxOut const&, int, int);
template uint256 SerializeHash<CTransaction>(CTransaction const&, int, int);

template<typename T1>
uint160 Hash160(const T1 pbegin, const T1 pend)
{
	static unsigned char pblank[1];
	uint256 hash1;
	uint160 hash2;

	SHA256(
		(pbegin == pend ? pblank : (unsigned char*)&pbegin[0]),
		(pend - pbegin) * sizeof(pbegin[0]),
		(unsigned char*)&hash1
	);

	RIPEMD160(
		(unsigned char*)&hash1,
		sizeof(hash1),
		(unsigned char*)&hash2
	);

	return hash2;
}

template uint160 Hash160<unsigned char const*>(unsigned char const*, unsigned char const*);

uint160 Hash160(const std::vector<unsigned char>& vch)
{
	return Hash160(vch.begin(), vch.end());
}

int HMAC_SHA512_Init(HMAC_SHA512_CTX *pctx, const void *pkey, size_t len)
{
	unsigned char key[128];
	if (len <= 128)
	{
		memcpy(key, pkey, len);
		memset(key + len, 0, 128-len);
	}
	else
	{
		SHA512_CTX ctxKey;
		SHA512_Init(&ctxKey);
		SHA512_Update(&ctxKey, pkey, len);
		SHA512_Final(key, &ctxKey);
		memset(key + 64, 0, 64);
	}

	for (int n=0; n<128; n++)
	{
		key[n] ^= 0x5c;
	}

	SHA512_Init(&pctx->ctxOuter);
	SHA512_Update(&pctx->ctxOuter, key, 128);

	for (int n=0; n<128; n++)
	{
		key[n] ^= 0x5c ^ 0x36;
	}

	SHA512_Init(&pctx->ctxInner);

	return SHA512_Update(&pctx->ctxInner, key, 128);
}

int HMAC_SHA512_Update(HMAC_SHA512_CTX *pctx, const void *pdata, size_t len)
{
	return SHA512_Update(&pctx->ctxInner, pdata, len);
}

int HMAC_SHA512_Final(unsigned char *pmd, HMAC_SHA512_CTX *pctx)
{
	unsigned char buf[64];

	SHA512_Final(buf, &pctx->ctxInner);
	SHA512_Update(&pctx->ctxOuter, buf, 64);

	return SHA512_Final(pmd, &pctx->ctxOuter);
}

void BIP32Hash(const unsigned char chainCode[32], unsigned int nChild, unsigned char header,
		const unsigned char data[32], unsigned char output[64])
{
	unsigned char num[4];
	HMAC_SHA512_CTX ctx;

	num[0] = (nChild >> 24) & 0xFF;
	num[1] = (nChild >> 16) & 0xFF;
	num[2] = (nChild >>  8) & 0xFF;
	num[3] = (nChild >>  0) & 0xFF;

	HMAC_SHA512_Init(&ctx, chainCode, 32);
	HMAC_SHA512_Update(&ctx, &header, 1);
	HMAC_SHA512_Update(&ctx, data, 32);
	HMAC_SHA512_Update(&ctx, num, 4);
	HMAC_SHA512_Final(output, &ctx);
}

