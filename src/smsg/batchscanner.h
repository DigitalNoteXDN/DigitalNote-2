#ifndef SMSG_BATCHSCANNER_H
#define SMSG_BATCHSCANNER_H

#include <string>
#include <leveldb/write_batch.h>

namespace leveldb {
	class Slice;
}

namespace DigitalNote {
namespace SMSG {

class BatchScanner : public leveldb::WriteBatch::Handler
{
public:
	std::string needle;
	bool* deleted;
	std::string* foundValue;
	bool foundEntry;

	BatchScanner();

	virtual void Put(const leveldb::Slice& key, const leveldb::Slice& value);
	virtual void Delete(const leveldb::Slice& key);
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_BATCHSCANNER_H
