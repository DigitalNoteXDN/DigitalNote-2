#ifndef SMSG_CDIGITALNOTEADRESS_B_H
#define SMSG_CDIGITALNOTEADRESS_B_H

#include "cdigitalnoteaddress.h"

namespace DigitalNote {
namespace SMSG {

class CDigitalNoteAddress_B : public CDigitalNoteAddress
{
public:
	uint8_t getVersion();
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_CDIGITALNOTEADRESS_B_H
