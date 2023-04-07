#ifndef CSPORKMESSAGE_H
#define CSPORKMESSAGE_H

#include <vector>
#include <cstdint>

class uint256;

//
// Spork Class
// Keeps track of all of the network spork settings
//

class CSporkMessage
{
public:
	std::vector<unsigned char> vchSig;
	int nSporkID;
	int64_t nValue;
	int64_t nTimeSigned;

	uint256 GetHash();

	size_t GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
	template <typename Stream, typename Operation>
	void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion);
};

#endif // CSPORKMESSAGE_H
