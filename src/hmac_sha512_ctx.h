#ifndef HMAC_SHA512_CTX_H
#define HMAC_SHA512_CTX_H

#include <openssl/sha.h>

typedef struct
{
	SHA512_CTX ctxInner;
	SHA512_CTX ctxOuter;
} HMAC_SHA512_CTX;

#endif // HMAC_SHA512_CTX_H
