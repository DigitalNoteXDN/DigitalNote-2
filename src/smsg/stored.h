#ifndef SMSG_STORED_H
#define SMSG_STORED_H

#include <cstdint>
#include <string>
#include <vector>

namespace DigitalNote {
namespace SMSG {

class Stored
{
public:
	int64_t timeReceived;
	char status;						// read etc
	uint16_t folderId;
	std::string sAddrTo;				// when in owned addr, when sent remote addr
	std::string sAddrOutbox;			// owned address this copy was encrypted with
	std::vector<uint8_t> vchMessage;	// message header + encryped payload

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_STORED_H
