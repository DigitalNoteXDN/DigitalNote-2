#ifndef NET_CSUBNET_H
#define NET_CSUBNET_H

#include <string>

#include "cnetaddr.h"

class CSubNet;

bool operator==(const CSubNet& a, const CSubNet& b);
bool operator!=(const CSubNet& a, const CSubNet& b);
bool operator<(const CSubNet& a, const CSubNet& b);

class CSubNet
{
protected:
	CNetAddr network;		// Network (base) address
	uint8_t netmask[16];	// Netmask, in network byte order
	bool valid;				// Is this value valid? (only used to signal parse errors)

public:
	CSubNet();
	explicit CSubNet(const std::string &strSubnet, bool fAllowLookup = false);

	bool Match(const CNetAddr &addr) const;

	std::string ToString() const;
	bool IsValid() const;

	friend bool operator==(const CSubNet& a, const CSubNet& b);
	friend bool operator!=(const CSubNet& a, const CSubNet& b);
	friend bool operator<(const CSubNet& a, const CSubNet& b);
	
	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // NET_CSUBNET_H
