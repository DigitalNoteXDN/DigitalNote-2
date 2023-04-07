#ifndef SMSG_CKEYID_B_H
#define SMSG_CKEYID_B_H

#include <cstdint>

#include "ckeyid.h"

namespace DigitalNote {
namespace SMSG {

class CKeyID_B : public CKeyID
{
public:
	uint32_t* GetPPN();
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_CKEYID_B_H
