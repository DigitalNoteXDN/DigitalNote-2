#ifndef SMSG_SECUREMESSAGE_H
#define SMSG_SECUREMESSAGE_H

#include <cstdint>

namespace DigitalNote {
namespace SMSG {

#pragma pack(push, 1)
class SecureMessage
{
public:
	uint8_t   hash[4];
	uint8_t   version[2];
	uint8_t   flags;
	int64_t   timestamp;
	uint8_t   iv[16];
	uint8_t   cpkR[33];
	uint8_t   mac[32];
	uint8_t   nonse[4];
	uint32_t  nPayload;
	uint8_t*  pPayload;

	SecureMessage();
	~SecureMessage();
};
#pragma pack(pop)

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_SECUREMESSAGE_H
