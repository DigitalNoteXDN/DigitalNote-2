#ifndef SMSG_DB_H
#define SMSG_DB_H

#include <cstdint>
#include <string>

#include <leveldb/db.h>

class CDataStream;
class CKeyID;
class CPubKey;

namespace DigitalNote {
namespace SMSG {
class Stored;

class DB
{
public:
	leveldb::DB* pdb;	// points to the global instance
	leveldb::WriteBatch* activeBatch;

	DB();
	~DB();

	bool Open(const char* pszMode="r+");
	bool ScanBatch(const CDataStream& key, std::string* value, bool* deleted) const;
	bool TxnBegin();
	bool TxnCommit();
	bool TxnAbort();
	bool ReadPK(CKeyID& addr, CPubKey& pubkey);
	bool WritePK(CKeyID& addr, CPubKey& pubkey);
	bool ExistsPK(CKeyID& addr);
	bool NextSmesg(leveldb::Iterator* it, std::string& prefix, uint8_t* vchKey, DigitalNote::SMSG::Stored& smsgStored);
	bool NextSmesgKey(leveldb::Iterator* it, std::string& prefix, uint8_t* vchKey);
	bool ReadSmesg(uint8_t* chKey, DigitalNote::SMSG::Stored& smsgStored);
	bool WriteSmesg(uint8_t* chKey, DigitalNote::SMSG::Stored& smsgStored);
	bool ExistsSmesg(uint8_t* chKey);
	bool EraseSmesg(uint8_t* chKey);
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_DB_H
