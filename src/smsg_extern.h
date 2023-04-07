#ifndef SMSG_EXTERN_H
#define SMSG_EXTERN_H

#include <cstdint>
#include <map>
#include <vector>
#include <boost/thread/thread.hpp>

#include "types/ccriticalsection.h"

namespace leveldb {
	class DB;
}

namespace DigitalNote
{
	namespace SMSG
	{
		class Address;
		class Stored;
		class Options;
		class Bucket;
		
		// Extern
		extern boost::thread_group							ext_thread_group;
		extern bool 										ext_enabled;
		extern std::map<int64_t, DigitalNote::SMSG::Bucket>	ext_buckets;
		extern std::vector<DigitalNote::SMSG::Address>		ext_addresses;
		extern DigitalNote::SMSG::Options					ext_options;
		extern CCriticalSection								ext_cs;				// all except inbox and outbox
		extern CCriticalSection								ext_cs_db;
		extern leveldb::DB*									ext_db;
	} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_EXTERN_H
