#ifndef CSTEALTHKEYMETADATA_H
#define CSTEALTHKEYMETADATA_H

#include "cpubkey.h"

class CStealthKeyMetadata
{
// -- used to get secret for keys created by stealth transaction with wallet locked
public:
	CPubKey pkEphem;
	CPubKey pkScan;

	CStealthKeyMetadata();
	CStealthKeyMetadata(CPubKey pkEphem_, CPubKey pkScan_);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream &s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream &s, int nType, int nVersion);
};

#endif // CSTEALTHKEYMETADATA_H
