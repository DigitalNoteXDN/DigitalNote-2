#ifndef SMSG_MESSAGE_H
#define SMSG_MESSAGE_H

#include <string>
#include <vector>

namespace DigitalNote {
namespace SMSG {
	
class Message
{
// -- Decrypted DigitalNote::SMSG::MessageSecure data
public:
	int64_t timestamp;
	std::string sToAddress;
	std::string sFromAddress;
	std::vector<uint8_t> vchMessage;	// null terminated plaintext
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_MESSAGE_H
