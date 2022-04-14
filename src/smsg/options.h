#ifndef SMSG_OPTIONS_H
#define SMSG_OPTIONS_H

namespace DigitalNote {
namespace SMSG {

class Options
{
public:
	bool fNewAddressRecv;
	bool fNewAddressAnon;
	bool fScanIncoming;

	Options();
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_OPTIONS_H
