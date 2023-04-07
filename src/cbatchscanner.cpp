#include "cbatchscanner.h"

CBatchScanner::CBatchScanner() : foundEntry(false)
{
	
}

void CBatchScanner::Put(const leveldb::Slice& key, const leveldb::Slice& value)
{
	if (key.ToString() == needle)
	{
		foundEntry = true;
		*deleted = false;
		*foundValue = value.ToString();
	}
}

void CBatchScanner::Delete(const leveldb::Slice& key)
{
	if (key.ToString() == needle)
	{
		foundEntry = true;
		*deleted = true;
	}
}

