#include <cstring>
#include <cassert>

#include "cpubkey.h"
#include "ckeyid.h"

#include "cextpubkey.h"

bool operator==(const CExtPubKey &a, const CExtPubKey &b)
{
	return (
		a.nDepth == b.nDepth &&
		memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0], 4) == 0 &&
		a.nChild == b.nChild &&
		memcmp(&a.vchChainCode[0], &b.vchChainCode[0], 32) == 0 &&
		a.pubkey == b.pubkey
	);
}

void CExtPubKey::Encode(unsigned char code[74]) const
{
	code[0] = nDepth;

	memcpy(code+1, vchFingerprint, 4);

	code[5] = (nChild >> 24) & 0xFF; code[6] = (nChild >> 16) & 0xFF;
	code[7] = (nChild >>  8) & 0xFF; code[8] = (nChild >>  0) & 0xFF;

	memcpy(code+9, vchChainCode, 32);

	assert(pubkey.size() == 33);

	memcpy(code+41, pubkey.begin(), 33);
}

void CExtPubKey::Decode(const unsigned char code[74])
{
	nDepth = code[0];

	memcpy(vchFingerprint, code+1, 4);

	nChild = (code[5] << 24) | (code[6] << 16) | (code[7] << 8) | code[8];

	memcpy(vchChainCode, code+9, 32);

	pubkey.Set(code+41, code+74);
}

bool CExtPubKey::Derive(CExtPubKey &out, unsigned int nChild) const
{
	out.nDepth = nDepth + 1;

	CKeyID id = pubkey.GetID();

	memcpy(&out.vchFingerprint[0], &id, 4);

	out.nChild = nChild;

	return pubkey.Derive(out.pubkey, out.vchChainCode, nChild, vchChainCode);
}

