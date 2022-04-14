#ifndef SMSG_ADDRESS_H
#define SMSG_ADDRESS_H

#include <string>

#include "string.h"

namespace DigitalNote {
namespace SMSG {

class Address
{
public:
	std::string sAddress;
	bool fReceiveEnabled;
	bool fReceiveAnon;

	Address();
	Address(std::string sAddr, bool receiveOn, bool receiveAnon);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_ADDRESS_H
