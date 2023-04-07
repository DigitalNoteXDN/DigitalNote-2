#include "compat.h"

#include <cstring>

#include "crypto/common/common.h"
#include "crypto/common/hmac_sha512.h"

#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include "util.h"
#include "chash256.h"
#include "ckeyid.h"
#include "cpubkey.h"
#include "cextpubkey.h"
#include "hmac_sha512_ctx.h"
#include "hash.h"
#include "allocators.h"
#include "cextkey.h"

bool operator==(const CExtKey &a, const CExtKey &b)
{
	return (
		a.nDepth == b.nDepth &&
		memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0], 4) == 0 &&
		a.nChild == b.nChild &&
		memcmp(&a.vchChainCode[0], &b.vchChainCode[0], 32) == 0 &&
		a.key == b.key
	);
}

bool CExtKey::Derive(CExtKey &out, unsigned int nChild) const
{
	out.nDepth = nDepth + 1;

	CKeyID id = key.GetPubKey().GetID();

	memcpy(&out.vchFingerprint[0], &id, 4);

	out.nChild = nChild;

	return key.Derive(out.key, out.vchChainCode, nChild, vchChainCode);
}

void CExtKey::SetMaster(const unsigned char *seed, unsigned int nSeedLen)
{
	static const char hashkey[] = {'B','i','t','c','o','i','n',' ','s','e','e','d'};

	HMAC_SHA512_CTX ctx;
	unsigned char out[64];

	HMAC_SHA512_Init(&ctx, hashkey, sizeof(hashkey));
	HMAC_SHA512_Update(&ctx, seed, nSeedLen);

	LockObject(out);

	HMAC_SHA512_Final(out, &ctx);

	key.Set(&out[0], &out[32], true);

	memcpy(vchChainCode, &out[32], 32);

	UnlockObject(out);

	nDepth = 0;
	nChild = 0;

	memset(vchFingerprint, 0, sizeof(vchFingerprint));
}

CExtPubKey CExtKey::Neuter() const
{
	CExtPubKey ret;

	ret.nDepth = nDepth;

	memcpy(&ret.vchFingerprint[0], &vchFingerprint[0], 4);

	ret.nChild = nChild;
	ret.pubkey = key.GetPubKey();

	memcpy(&ret.vchChainCode[0], &vchChainCode[0], 32);

	return ret;
}

void CExtKey::Encode(unsigned char code[74]) const
{
	code[0] = nDepth;

	memcpy(code+1, vchFingerprint, 4);

	code[5] = (nChild >> 24) & 0xFF; code[6] = (nChild >> 16) & 0xFF;
	code[7] = (nChild >>  8) & 0xFF; code[8] = (nChild >>  0) & 0xFF;

	memcpy(code+9, vchChainCode, 32);

	code[41] = 0;

	assert(key.size() == 32);

	memcpy(code+42, key.begin(), 32);
}

void CExtKey::Decode(const unsigned char code[74])
{
	nDepth = code[0];

	memcpy(vchFingerprint, code+1, 4);

	nChild = (code[5] << 24) | (code[6] << 16) | (code[7] << 8) | code[8];

	memcpy(vchChainCode, code+9, 32);

	key.Set(code+42, code+74, true);
}

