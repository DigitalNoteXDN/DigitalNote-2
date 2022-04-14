#ifndef CEXTPUBKEY_H
#define CEXTPUBKEY_H

#include "cpubkey.h"

struct CExtPubKey;

bool operator==(const CExtPubKey &a, const CExtPubKey &b);

struct CExtPubKey
{
	unsigned char nDepth;
	unsigned char vchFingerprint[4];
	unsigned int nChild;
	unsigned char vchChainCode[32];
	CPubKey pubkey;

	friend bool operator==(const CExtPubKey &a, const CExtPubKey &b);

	void Encode(unsigned char code[74]) const;
	void Decode(const unsigned char code[74]);
	bool Derive(CExtPubKey &out, unsigned int nChild) const;
};

#endif // CEXTPUBKEY_H
