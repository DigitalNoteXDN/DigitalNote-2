#ifndef CBATCHSCANNER_H
#define CBATCHSCANNER_H

#include <string>

#include <leveldb/write_batch.h>

class CBatchScanner : public leveldb::WriteBatch::Handler
{
public:
	std::string needle;
	bool *deleted;
	std::string *foundValue;
	bool foundEntry;

	CBatchScanner();
	virtual void Put(const leveldb::Slice& key, const leveldb::Slice& value);
	virtual void Delete(const leveldb::Slice& key);
};

#endif // CBATCHSCANNER_H
