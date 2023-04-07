#ifndef CSIZECOMPUTER_H
#define CSIZECOMPUTER_H

#include <stddef.h>

class CSizeComputer
{
protected:
	size_t nSize;

public:
	int nType;
	int nVersion;

	CSizeComputer(int nTypeIn, int nVersionIn);

	CSizeComputer& write(const char *psz, size_t nSize);

	template<typename T>
	CSizeComputer& operator<<(const T& obj);

	size_t size() const;
};

#endif // CSIZECOMPUTER_H
