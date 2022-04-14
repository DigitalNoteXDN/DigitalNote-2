#include "compat.h"

#include <boost/thread.hpp>

#include "util.h"
#include "thread.h"

#include "cdbenv.h"

#ifndef WIN32
	#include <sys/stat.h>	
#endif

void CDBEnv::EnvShutdown()
{
	if (!fDbEnvInit)
	{
		return;
	}

	fDbEnvInit = false;
	int ret = dbenv.close(0);

	if (ret != 0)
	{
		LogPrintf("EnvShutdown exception: %s (%d)\n", DbEnv::strerror(ret), ret);
	}

	if (!fMockDb)
	{
		// DbEnv(0).remove(strPath.c_str(), 0);
		DbEnv((u_int32_t)0).remove(strPath.c_str(), 0);
	}
}

CDBEnv::CDBEnv() : dbenv(DB_CXX_NO_EXCEPTIONS)
{
	fDbEnvInit = false;
	fMockDb = false;
}

CDBEnv::~CDBEnv()
{
	EnvShutdown();
}

void CDBEnv::MakeMock()
{
	if (fDbEnvInit)
	{
		throw std::runtime_error("CDBEnv::MakeMock(): already initialized");
	}

	boost::this_thread::interruption_point();

	LogPrint("db", "CDBEnv::MakeMock()\n");

	dbenv.set_cachesize(1, 0, 1);
	dbenv.set_lg_bsize(10485760*4);
	dbenv.set_lg_max(10485760);
	dbenv.set_lk_max_locks(10000);
	dbenv.set_lk_max_objects(10000);
	dbenv.set_flags(DB_AUTO_COMMIT, 1);
	
#ifdef DB_LOG_IN_MEMORY
	dbenv.log_set_config(DB_LOG_IN_MEMORY, 1);
#endif

	int ret = dbenv.open(NULL,
					 DB_CREATE |
					 DB_INIT_LOCK |
					 DB_INIT_LOG |
					 DB_INIT_MPOOL |
					 DB_INIT_TXN |
					 DB_THREAD |
					 DB_PRIVATE,
					 S_IRUSR | S_IWUSR);
	if (ret > 0)
	{
		throw std::runtime_error(strprintf("CDBEnv::MakeMock(): error %d opening database environment", ret));
	}

	fDbEnvInit = true;
	fMockDb = true;
}

bool CDBEnv::IsMock()
{
	return fMockDb;
}

CDBEnv::VerifyResult CDBEnv::Verify(std::string strFile, bool (*recoverFunc)(CDBEnv& dbenv, std::string strFile))
{
	LOCK(cs_db);

	assert(mapFileUseCount.count(strFile) == 0);

	Db db(&dbenv, 0);
	int result = db.verify(strFile.c_str(), NULL, NULL, 0);

	if (result == 0)
	{
		return VERIFY_OK;
	}
	else if (recoverFunc == NULL)
	{
		return RECOVER_FAIL;
	}

	// Try to recover:
	bool fRecovered = (*recoverFunc)(*this, strFile);

	return (fRecovered ? RECOVER_OK : RECOVER_FAIL);
}

bool CDBEnv::Salvage(std::string strFile, bool fAggressive, std::vector<CDBEnv::KeyValPair >& vResult)
{
	LOCK(cs_db);

	assert(mapFileUseCount.count(strFile) == 0);

	u_int32_t flags = DB_SALVAGE;

	if (fAggressive)
	{
		flags |= DB_AGGRESSIVE;
	}

	std::stringstream strDump;
	Db db(&dbenv, 0);
	int result = db.verify(strFile.c_str(), NULL, &strDump, flags);

	if (result == DB_VERIFY_BAD)
	{
		LogPrintf("Error: Salvage found errors, all data may not be recoverable.\n");
		
		if (!fAggressive)
		{
			LogPrintf("Error: Rerun with aggressive mode to ignore errors and continue.\n");
			
			return false;
		}
	}

	if (result != 0 && result != DB_VERIFY_BAD)
	{
		LogPrintf("ERROR: db salvage failed: %d\n",result);
		
		return false;
	}

	// Format of bdb dump is ascii lines:
	// header lines...
	// HEADER=END
	// hexadecimal key
	// hexadecimal value
	// ... repeated
	// DATA=END

	std::string strLine;

	while (!strDump.eof() && strLine != "HEADER=END")
	{
		getline(strDump, strLine); // Skip past header
	}

	std::string keyHex, valueHex;

	while (!strDump.eof() && keyHex != "DATA=END")
	{
		getline(strDump, keyHex);
		
		if (keyHex != "DATA_END")
		{
			getline(strDump, valueHex);
			
			vResult.push_back(std::make_pair(ParseHex(keyHex),ParseHex(valueHex)));
		}
	}

	return (result == 0);
}

bool CDBEnv::Open(boost::filesystem::path pathEnv_)
{
	if(fDbEnvInit)
	{
		return true;
	}
	
	boost::this_thread::interruption_point();

	pathEnv = pathEnv_;
	boost::filesystem::path pathDataDir = pathEnv;
	strPath = pathDataDir.string();

	boost::filesystem::path pathLogDir = pathDataDir / "database";
	boost::filesystem::create_directory(pathLogDir);
	boost::filesystem::path pathErrorFile = pathDataDir / "db.log";

	LogPrintf("dbenv.open LogDir=%s ErrorFile=%s\n", pathLogDir.string(), pathErrorFile.string());

	unsigned int nEnvFlags = 0;

	if (GetBoolArg("-privdb", true))
	{
		nEnvFlags |= DB_PRIVATE;
	}

	int nDbCache = GetArg("-dbcache", 100);

	dbenv.set_lg_dir(pathLogDir.string().c_str());
	dbenv.set_cachesize(nDbCache / 1024, (nDbCache % 1024) * 1048576, 1);
	dbenv.set_lg_bsize(1048576);
	dbenv.set_lg_max(10485760);

	// Bugfix: Bump lk_max_locks default to 537000, to safely handle reorgs with up to 5 blocks reversed
	// dbenv.set_lk_max_locks(10000);
	dbenv.set_lk_max_locks(537000);

	dbenv.set_lk_max_objects(10000);
	dbenv.set_errfile(fopen(pathErrorFile.string().c_str(), "a")); /// debug
	dbenv.set_flags(DB_AUTO_COMMIT, 1);
	dbenv.set_flags(DB_TXN_WRITE_NOSYNC, 1);

#ifdef DB_LOG_AUTO_REMOVE
	dbenv.log_set_config(DB_LOG_AUTO_REMOVE, 1);
#endif

	int ret = dbenv.open(strPath.c_str(),
					 DB_CREATE |
					 DB_INIT_LOCK |
					 DB_INIT_LOG |
					 DB_INIT_MPOOL |
					 DB_INIT_TXN |
					 DB_THREAD |
					 DB_RECOVER |
					 nEnvFlags,
					 S_IRUSR | S_IWUSR);

	if (ret != 0)
	{
		return error("CDB() : error %s (%d) opening database environment", DbEnv::strerror(ret), ret);
	}

	fDbEnvInit = true;
	fMockDb = false;

	return true;
}

void CDBEnv::Close()
{
	EnvShutdown();
}

void CDBEnv::Flush(bool fShutdown)
{
	int64_t nStart = GetTimeMillis();

	// Flush log data to the actual data file
	//  on all files that are not in use
	LogPrint("db", "Flush(%s)%s\n", fShutdown ? "true" : "false", fDbEnvInit ? "" : " db not started");

	if (!fDbEnvInit)
	{
		return;
	}

	{
		LOCK(cs_db);
		
		std::map<std::string, int>::iterator mi = mapFileUseCount.begin();
		
		while (mi != mapFileUseCount.end())
		{
			std::string strFile = (*mi).first;
			int nRefCount = (*mi).second;
			
			LogPrint("db", "%s refcount=%d\n", strFile, nRefCount);
			
			if (nRefCount == 0)
			{
				// Move log data to the dat file
				CloseDb(strFile);
				
				LogPrint("db", "%s checkpoint\n", strFile);
				
				dbenv.txn_checkpoint(0, 0, 0);
				
				LogPrint("db", "%s detach\n", strFile);
				
				if (!fMockDb)
				{
					dbenv.lsn_reset(strFile.c_str(), 0);
				}
				
				LogPrint("db", "%s closed\n", strFile);
				
				mapFileUseCount.erase(mi++);
			}
			else
			{
				mi++;
			}
		}
		
		LogPrint("db", "DBFlush(%s)%s ended %15dms\n", fShutdown ? "true" : "false", fDbEnvInit ? "" : " db not started", GetTimeMillis() - nStart);
		
		if (fShutdown)
		{
			char** listp;
			
			if (mapFileUseCount.empty())
			{
				dbenv.log_archive(&listp, DB_ARCH_REMOVE);
				Close();
			}
		}
	}
}

void CDBEnv::CheckpointLSN(const std::string& strFile)
{
	dbenv.txn_checkpoint(0, 0, 0);

	if (fMockDb)
	{
		return;
	}

	dbenv.lsn_reset(strFile.c_str(), 0);
}

void CDBEnv::CloseDb(const std::string& strFile)
{
	{
		LOCK(cs_db);
		
		if (mapDb[strFile] != NULL)
		{
			// Close the database handle
			Db* pdb = mapDb[strFile];
			
			pdb->close(0);
			delete pdb;
			mapDb[strFile] = NULL;
		}
	}
}

bool CDBEnv::RemoveDb(const std::string& strFile)
{
	this->CloseDb(strFile);

	LOCK(cs_db);

	int rc = dbenv.dbremove(NULL, strFile.c_str(), NULL, DB_AUTO_COMMIT);

	return (rc == 0);
}

DbTxn* CDBEnv::TxnBegin(int flags)
{
	DbTxn* ptxn = NULL;
	int ret = dbenv.txn_begin(NULL, &ptxn, flags);

	if (!ptxn || ret != 0)
	{
		return NULL;
	}

	return ptxn;
}

