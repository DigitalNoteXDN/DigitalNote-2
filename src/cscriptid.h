#ifndef CSCRIPTID_H
#define CSCRIPTID_H

#include "uint/uint160.h"

/** A reference to a CScript: the Hash160 of its serialization (see script.h) */
class CScriptID : public uint160
{
public:
	CScriptID() : uint160(0)
	{
		
	}

	CScriptID(const uint160 &in) : uint160(in)
	{
		
	}
};

#endif // CSCRIPTID_H
