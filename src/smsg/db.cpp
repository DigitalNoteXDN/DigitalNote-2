#include "compat.h"

#include <boost/filesystem.hpp>

#include "util.h"
#include "smsg_extern.h"
#include "smsg/batchscanner.h"
#include "smsg/stored.h"
#include "ckeyid.h"
#include "cpubkey.h"
#include "enums/serialize_type.h"
#include "version.h"
#include "cdatastream.h"

#include "smsg/db.h"

namespace DigitalNote {
namespace SMSG {

DB::DB()
{
	activeBatch = NULL;
}

DB::~DB()
{
	// -- deletes only data scoped to this TxDB object.
	if (activeBatch)
	{
		delete activeBatch;
	}
}

bool DB::Open(const char* pszMode)
{
	if (DigitalNote::SMSG::ext_db)
	{
		pdb = DigitalNote::SMSG::ext_db;
		
		return true;
	}

	bool fCreate = strchr(pszMode, 'c');

	boost::filesystem::path fullpath = GetDataDir() / "smsgDB";

	if (!fCreate
		&& (!boost::filesystem::exists(fullpath)
			|| !boost::filesystem::is_directory(fullpath)))
	{
		LogPrint("smsg", "DigitalNote::SMSG::DB::open() - DB does not exist.\n");
		
		return false;
	}

	leveldb::Options options;
	options.create_if_missing = fCreate;
	leveldb::Status s = leveldb::DB::Open(options, fullpath.string(), &DigitalNote::SMSG::ext_db);

	if (!s.ok())
	{
		LogPrint("smsg", "DigitalNote::SMSG::DB::open() - Error opening db: %s.\n", s.ToString().c_str());
		return false;
	};

	pdb = DigitalNote::SMSG::ext_db;

	return true;
}

// When performing a read, if we have an active batch we need to check it first
// before reading from the database, as the rest of the code assumes that once
// a database transaction begins reads are consistent with it. It would be good
// to change that assumption in future and avoid the performance hit, though in
// practice it does not appear to be large.
bool DB::ScanBatch(const CDataStream& key, std::string* value, bool* deleted) const
{
	if (!activeBatch)
	{
		return false;
	}
	
	*deleted = false;
	
	DigitalNote::SMSG::BatchScanner scanner;
	scanner.needle = key.str();
	scanner.deleted = deleted;
	scanner.foundValue = value;
	
	leveldb::Status s = activeBatch->Iterate(&scanner);
	
	if (!s.ok())
	{
		LogPrint("smsg", "DigitalNote::SMSG::DB ScanBatch error: %s\n", s.ToString().c_str());
		return false;
	};

	return scanner.foundEntry;
}

bool DB::TxnBegin()
{
	if (activeBatch)
	{
		return true;
	}
	
	activeBatch = new leveldb::WriteBatch();
	
	return true;
}

bool DB::TxnCommit()
{
	if (!activeBatch)
	{
		return false;
	}

	leveldb::WriteOptions writeOptions;
	writeOptions.sync = true;
	
	leveldb::Status status = pdb->Write(writeOptions, activeBatch);
	
	delete activeBatch;
	activeBatch = NULL;

	if (!status.ok())
	{
		LogPrint("smsg", "DigitalNote::SMSG::DB batch commit failure: %s\n", status.ToString().c_str());
		
		return false;
	}

	return true;
}

bool DB::TxnAbort()
{
	delete activeBatch;

	activeBatch = NULL;

	return true;
}

bool DB::ReadPK(CKeyID& addr, CPubKey& pubkey)
{
	if (!pdb)
	{
		return false;
	}

	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	
	ssKey.reserve(sizeof(addr) + 2);
	ssKey << 'p';
	ssKey << 'k';
	ssKey << addr;
	
	std::string strValue;

	bool readFromDb = true;
	
	if (activeBatch)
	{
		// -- check activeBatch first
		bool deleted = false;
		
		readFromDb = ScanBatch(ssKey, &strValue, &deleted) == false;
		
		if (deleted)
		{
			return false;
		}
	}

	if (readFromDb)
	{
		leveldb::Status s = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &strValue);
		
		if (!s.ok())
		{
			if (s.IsNotFound())
			{
				return false;
			}
			
			LogPrint("smsg", "LevelDB read failure: %s\n", s.ToString().c_str());
			
			return false;
		}
	}

	try
	{
		CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
		ssValue >> pubkey;
	}
	catch (std::exception& e)
	{
		LogPrint("smsg", "DigitalNote::SMSG::DB::ReadPK() unserialize threw: %s.\n", e.what());
		
		return false;
	}

	return true;
}

bool DB::WritePK(CKeyID& addr, CPubKey& pubkey)
{
	if (!pdb)
	{
		return false;
	}

	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	ssKey.reserve(sizeof(addr) + 2);
	ssKey << 'p';
	ssKey << 'k';
	ssKey << addr;
	
	CDataStream ssValue(SER_DISK, CLIENT_VERSION);
	
	ssValue.reserve(sizeof(pubkey));
	ssValue << pubkey;

	if (activeBatch)
	{
		activeBatch->Put(ssKey.str(), ssValue.str());
		
		return true;
	}

	leveldb::WriteOptions writeOptions;
	writeOptions.sync = true;
	
	leveldb::Status s = pdb->Put(writeOptions, ssKey.str(), ssValue.str());
	
	if (!s.ok())
	{
		LogPrint("smsg", "DigitalNote::SMSG::DB write failure: %s\n", s.ToString().c_str());
		
		return false;
	}

	return true;
}

bool DB::ExistsPK(CKeyID& addr)
{
	if (!pdb)
	{
		return false;
	}
	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	
	ssKey.reserve(sizeof(addr)+2);
	ssKey << 'p';
	ssKey << 'k';
	ssKey << addr;
	
	std::string unused;

	if (activeBatch)
	{
		bool deleted;
		
		if (ScanBatch(ssKey, &unused, &deleted) && !deleted)
		{
			return true;
		}
	}

	leveldb::Status s = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &unused);
	
	return s.IsNotFound() == false;
}

bool DB::NextSmesg(leveldb::Iterator* it, std::string& prefix, uint8_t* chKey, DigitalNote::SMSG::Stored& smsgStored)
{
	if (!pdb)
	{
		return false;
	}
	
	if (!it->Valid()) // first run
	{
		it->Seek(prefix);
	}
	else
	{
		it->Next();
	}
	
	if (!(it->Valid()
		&& it->key().size() == 18
		&& memcmp(it->key().data(), prefix.data(), 2) == 0))
		return false;

	memcpy(chKey, it->key().data(), 18);

	try
	{
		CDataStream ssValue(it->value().data(), it->value().data() + it->value().size(), SER_DISK, CLIENT_VERSION);
		ssValue >> smsgStored;
	}
	catch (std::exception& e)
	{
		LogPrint("smsg", "DigitalNote::SMSG::DB::NextSmesg() unserialize threw: %s.\n", e.what());
		
		return false;
	}

	return true;
}

bool DB::NextSmesgKey(leveldb::Iterator* it, std::string& prefix, uint8_t* chKey)
{
	if (!pdb)
	{
		return false;
	}

	if (!it->Valid()) // first run
	{
		it->Seek(prefix);
	}
	else
	{
		it->Next();
	}

	if (!(it->Valid()
		&& it->key().size() == 18
		&& memcmp(it->key().data(), prefix.data(), 2) == 0))
		{
		return false;
	}
		
	memcpy(chKey, it->key().data(), 18);

	return true;
}

bool DB::ReadSmesg(uint8_t* chKey, DigitalNote::SMSG::Stored& smsgStored)
{
	if (!pdb)
	{
		return false;
	}
	
	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	ssKey.write((const char*)chKey, 18);
	
	std::string strValue;
	bool readFromDb = true;
	
	if (activeBatch)
	{
		// -- check activeBatch first
		bool deleted = false;
		
		readFromDb = ScanBatch(ssKey, &strValue, &deleted) == false;
		
		if (deleted)
		{
			return false;
		}
	}

	if (readFromDb)
	{
		leveldb::Status s = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &strValue);
		
		if (!s.ok())
		{
			if (s.IsNotFound())
			{
				return false;
			}
			
			LogPrint("smsg", "LevelDB read failure: %s\n", s.ToString().c_str());
			
			return false;
		}
	}

	try
	{
		CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
		ssValue >> smsgStored;
	}
	catch (std::exception& e) {
		LogPrint("smsg", "DigitalNote::SMSG::DB::ReadSmesg() unserialize threw: %s.\n", e.what());
		
		return false;
	}

	return true;
}

bool DB::WriteSmesg(uint8_t* chKey, DigitalNote::SMSG::Stored& smsgStored)
{
	if (!pdb)
	{
		return false;
	}

	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	ssKey.write((const char*)chKey, 18);

	CDataStream ssValue(SER_DISK, CLIENT_VERSION);
	ssValue << smsgStored;

	if (activeBatch)
	{
		activeBatch->Put(ssKey.str(), ssValue.str());
		
		return true;
	}

	leveldb::WriteOptions writeOptions;
	writeOptions.sync = true;
	
	leveldb::Status s = pdb->Put(writeOptions, ssKey.str(), ssValue.str());
	
	if (!s.ok())
	{
		LogPrint("smsg", "DigitalNote::SMSG::DB write failed: %s\n", s.ToString().c_str());
		
		return false;
	}

	return true;
}

bool DB::ExistsSmesg(uint8_t* chKey)
{
	if (!pdb)
	{
		return false;
	}
	
	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	ssKey.write((const char*)chKey, 18);
	
	std::string unused;

	if (activeBatch)
	{
		bool deleted;
		
		if (ScanBatch(ssKey, &unused, &deleted) && !deleted)
		{
			return true;
		}
	}

	leveldb::Status s = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &unused);
	
	return s.IsNotFound() == false;
}

bool DB::EraseSmesg(uint8_t* chKey)
{
	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	ssKey.write((const char*)chKey, 18);

	if (activeBatch)
	{
		activeBatch->Delete(ssKey.str());
		
		return true;
	}

	leveldb::WriteOptions writeOptions;
	writeOptions.sync = true;
	
	leveldb::Status s = pdb->Delete(writeOptions, ssKey.str());

	if (s.ok() || s.IsNotFound())
	{
		return true;
	}
	
	LogPrint("smsg", "DigitalNote::SMSG::DB erase failed: %s\n", s.ToString().c_str());
	
	return false;
}

} // namespace SMSG
} // namespace DigitalNote

