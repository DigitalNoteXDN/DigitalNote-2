#ifndef CBASE58DATA_H
#define CBASE58DATA_H

#include <string>
#include <vector>

#include "types/vector_uchar.h"

/**
 * Base class for all base58-encoded data
 */
class CBase58Data
{
protected:
	// the version byte(s)
	std::vector<unsigned char> vchVersion;

	// the actually encoded data
	vector_uchar vchData;

	CBase58Data();

	void SetData(const std::vector<unsigned char> &vchVersionIn, const void* pdata, size_t nSize);
	void SetData(const std::vector<unsigned char> &vchVersionIn, const unsigned char *pbegin, const unsigned char *pend);

public:
	bool SetString(const char* psz, unsigned int nVersionBytes = 1);
	bool SetString(const std::string& str);
	std::string ToString() const;
	int CompareTo(const CBase58Data& b58) const;

	bool operator==(const CBase58Data& b58) const;
	bool operator<=(const CBase58Data& b58) const;
	bool operator>=(const CBase58Data& b58) const;
	bool operator< (const CBase58Data& b58) const;
	bool operator> (const CBase58Data& b58) const;
};

#endif // CBASE58DATA_H
