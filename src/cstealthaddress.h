#ifndef CSTEALTHADDRESS_H
#define CSTEALTHADDRESS_H

#include <string>

#include "types/ec_point.h"
#include "types/data_chunk.h"
#include "stealth.h"

class CPubKey;

class CStealthAddress
{
public:
	uint8_t options;
	ec_point scan_pubkey;
	ec_point spend_pubkey;
	//std::vector<ec_point> spend_pubkeys;
	size_t number_signatures;
	stealth_prefix prefix;
	mutable std::string label;
	data_chunk scan_secret;
	data_chunk spend_secret;

	CStealthAddress();

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	bool SetEncoded(const std::string& encodedAddress);
	std::string Encoded() const;
	int SetScanPubKey(CPubKey pk);
	bool operator<(const CStealthAddress& y) const;
};

#endif // CSTEALTHADDRESS_H
