#ifndef CFLATDATA_H
#define CFLATDATA_H

#include "serialize/base.h"

#define FLATDATA(obj)  REF(CFlatData((char*)&(obj), (char*)&(obj) + sizeof(obj)))

/** Wrapper for serializing arrays and POD.
 */
class CFlatData
{
protected:
	char* pbegin;
	char* pend;

public:
	CFlatData(void* pbeginIn, void* pendIn);

	char* begin();
	const char* begin() const;
	char* end();
	const char* end() const;
	unsigned int GetSerializeSize(int, int = 0) const;

	template<typename Stream>
	void Serialize(Stream& s, int, int = 0) const;

	template<typename Stream>
	void Unserialize(Stream& s, int, int = 0);
};

#endif // CFLATDATA_H
