#ifndef CEXTKEY_H
#define CEXTKEY_H

#include "ckey.h"

struct CExtKey;
struct CExtPubKey;

bool operator==(const CExtKey &a, const CExtKey &b);

struct CExtKey
{
	unsigned char nDepth;
	unsigned char vchFingerprint[4];
	unsigned int nChild;
	unsigned char vchChainCode[32];
	CKey key;

	friend bool operator==(const CExtKey &a, const CExtKey &b);

	void Encode(unsigned char code[74]) const;
	void Decode(const unsigned char code[74]);
	bool Derive(CExtKey &out, unsigned int nChild) const;
	CExtPubKey Neuter() const;
	void SetMaster(const unsigned char *seed, unsigned int nSeedLen);
};

#endif // CEXTKEY_H
