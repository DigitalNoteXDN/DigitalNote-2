#ifndef CKEYID_H
#define CKEYID_H

#include "uint/uint160.h"

/** A reference to a CKey: the Hash160 of its serialized public key */
class CKeyID : public uint160
{
public:
	CKeyID() : uint160(0)
	{
		
	}

	CKeyID(const uint160 &in) : uint160(in)
	{
		
	}
};

#endif // CKEYID_H
