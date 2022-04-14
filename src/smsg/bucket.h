#ifndef SMSG_BUCKET_H
#define SMSG_BUCKET_H

#include <cstdint>
#include <set>

#include "smsg/token.h"
#include "types/nodeid.h"

namespace DigitalNote {
namespace SMSG {

class Bucket
{
public:
	int64_t timeChanged;
	uint32_t hash;			// token set should get ordered the same on each node
	uint32_t nLockCount;	// set when smsgWant first sent, unset at end of smsgMsg, ticks down in ThreadSecureMsg()
	NodeId nLockPeerId;		// id of peer that bucket is locked for
	std::set<DigitalNote::SMSG::Token> setTokens;

	Bucket();
	~Bucket();
	void hashBucket();
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_BUCKET_H
