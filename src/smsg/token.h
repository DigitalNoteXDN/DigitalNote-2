#ifndef SMSG_TOKEN_H
#define SMSG_TOKEN_H

#include <cstdint>

namespace DigitalNote {
namespace SMSG {

class Token
{
public:
	int64_t timestamp;	// doesn't need to be full 64 bytes?
	uint8_t sample[8];	// first 8 bytes of payload - a hash
	int64_t offset;		// offset

	Token();
	Token(int64_t ts, uint8_t* p, int np, long int o);
	~Token();

	bool operator <(const Token& y) const;
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_TOKEN_H
