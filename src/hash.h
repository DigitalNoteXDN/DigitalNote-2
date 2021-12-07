#ifndef HASH_H
#define HASH_H

#include <vector>

#include "enums/serialize_type.h"
#include "version.h"
#include "hmac_sha512_ctx.h"

class uint256;
class uint160;

template<typename T1>
uint256 Hash(const T1 pbegin, const T1 pend);

template<typename T1, typename T2>
uint256 Hash(const T1 p1begin, const T1 p1end, const T2 p2begin, const T2 p2end);

template<typename T1, typename T2, typename T3>
uint256 Hash(const T1 p1begin, const T1 p1end, const T2 p2begin, const T2 p2end, const T3 p3begin, const T3 p3end);

template<typename T>
uint256 SerializeHash(const T& obj, int nType=SER_GETHASH, int nVersion=PROTOCOL_VERSION);

template<typename T1>
uint160 Hash160(const T1 pbegin, const T1 pend);

uint160 Hash160(const std::vector<unsigned char>& vch);

int HMAC_SHA512_Init(HMAC_SHA512_CTX *pctx, const void *pkey, size_t len);
int HMAC_SHA512_Update(HMAC_SHA512_CTX *pctx, const void *pdata, size_t len);
int HMAC_SHA512_Final(unsigned char *pmd, HMAC_SHA512_CTX *pctx);

void BIP32Hash(const unsigned char chainCode[32], unsigned int nChild, unsigned char header,
		const unsigned char data[32], unsigned char output[64]);

#endif // HASH_H
