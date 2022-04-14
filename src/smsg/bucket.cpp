#include "compat.h"

#include "util.h"
#include "xxhash/xxhash.h"
#include "xxhash/xxhash.c"

#include "smsg/bucket.h"

namespace DigitalNote {
namespace SMSG {

Bucket::Bucket()
{
	timeChanged = 0;
	hash = 0;
	nLockCount = 0;
	nLockPeerId = 0;
}

Bucket::~Bucket()
{
	
}

void Bucket::hashBucket()
{
	if (fDebugSmsg)
	{
		LogPrint("smsg", "DigitalNote::SMSG::Bucket::hashBucket()\n");
	}

	timeChanged = GetTime();

	std::set<DigitalNote::SMSG::Token>::iterator it;

	void* state = XXH32_init(1);

	for (it = setTokens.begin(); it != setTokens.end(); ++it)
	{
		XXH32_update(state, it->sample, 8);
	}

	hash = XXH32_digest(state);

	if (fDebugSmsg)
	{
		LogPrint("smsg", "Hashed %u messages, hash %u\n", setTokens.size(), hash);
	}
}

} // namespace SMSG
} // namespace DigitalNote
