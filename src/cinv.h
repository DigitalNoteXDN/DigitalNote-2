#ifndef CINV_H
#define CINV_H

#include "uint/uint256.h"

class CInv;

bool operator<(const CInv& a, const CInv& b);

/** inv message data */
class CInv
{
public:
	int type;
	uint256 hash;

	CInv();
	CInv(int typeIn, const uint256& hashIn);
	CInv(const std::string& strType, const uint256& hashIn);

	friend bool operator<(const CInv& a, const CInv& b);

	bool IsKnownType() const;
	const char* GetCommand() const;
	std::string ToString() const;

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CINV_H
