#include "compat.h"

#include <boost/tuple/tuple.hpp>

#include "util.h"
#include "db.h"
#include "enums/serialize_type.h"
#include "thread.h"
#include "ckeymetadata.h"
#include "cstealthaddress.h"
#include "types/cprivkey.h"
#include "stealth.h"
#include "cscript.h"
#include "caccountingentry.h"
#include "caccount.h"
#include "ckeypool.h"
#include "cblocklocator.h"
#include "cstealthkeymetadata.h"
#include "cwallettx.h"
#include "cdbenv.h"
#include "cmasterkey.h"
#include "ckeyid.h"
#include "version.h"
#include "cdatastream.h"

#include "cdb.h"

CDB::CDB(const std::string& strFilename, const char* pszMode) : pdb(NULL), activeTxn(NULL)
{
    int ret;
    
	fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));
    
	if (strFilename.empty())
	{
        return;
	}
	
    bool fCreate = strchr(pszMode, 'c');
    unsigned int nFlags = DB_THREAD;
    
	if (fCreate)
	{
        nFlags |= DB_CREATE;
	}
	
    {
        LOCK(bitdb.cs_db);
        
		if (!bitdb.Open(GetDataDir()))
		{
            throw std::runtime_error("env open failed");
		}
		
        strFile = strFilename;
        ++bitdb.mapFileUseCount[strFile];
        pdb = bitdb.mapDb[strFile];
		
        if (pdb == NULL)
        {
            pdb = new Db(&bitdb.dbenv, 0);

            bool fMockDb = bitdb.IsMock();
            
			if (fMockDb)
            {
                DbMpoolFile*mpf = pdb->get_mpf();
                
				ret = mpf->set_flags(DB_MPOOL_NOFILE, 1);
				
                if (ret != 0)
				{
                    throw std::runtime_error(strprintf("CDB : Failed to configure for no temp file backing for database %s", strFile));
				}
            }

            ret = pdb->open(NULL,								// Txn pointer
                            fMockDb ? NULL : strFile.c_str(),	// Filename
                            fMockDb ? strFile.c_str() : "main",	// Logical db name
                            DB_BTREE,							// Database type
                            nFlags,								// Flags
                            0);

            if (ret != 0)
            {
                delete pdb;
				pdb = NULL;
                
				--bitdb.mapFileUseCount[strFile];
                
				strFile = "";
                
				throw std::runtime_error(strprintf("CDB : Error %d, can't open database %s", ret, strFile));
            }

            if (fCreate && !Exists(std::string("version")))
            {
                bool fTmp = fReadOnly;
				
                fReadOnly = false;
                
				WriteVersion(CLIENT_VERSION);
                
				fReadOnly = fTmp;
            }

            bitdb.mapDb[strFile] = pdb;
        }
    }
}

CDB::~CDB()
{
	Close();
}

void CDB::Close()
{
    if (!pdb)
	{
        return;
	}
	
    if (activeTxn)
	{
        activeTxn->abort();
    }
	
	activeTxn = NULL;
    pdb = NULL;

    // Flush database activity from memory pool to disk log
    unsigned int nMinutes = 0;
    if (fReadOnly)
	{
        nMinutes = 1;
	}
	
    bitdb.dbenv.txn_checkpoint(nMinutes ? GetArg("-dblogsize", 100)*1024 : 0, nMinutes, 0);

    {
        LOCK(bitdb.cs_db);
		
        --bitdb.mapFileUseCount[strFile];
    }
}

template<typename K, typename T>
bool CDB::Read(const K& key, T& value)
{
	if (!pdb)
	{
		return false;
	}
	
	// Key
	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	
	ssKey.reserve(1000);
	ssKey << key;
	
	Dbt datKey(&ssKey[0], ssKey.size());

	// Read
	Dbt datValue;
	datValue.set_flags(DB_DBT_MALLOC);
	int ret = pdb->get(activeTxn, &datKey, &datValue, 0);
	
	memset(datKey.get_data(), 0, datKey.get_size());
	
	if (datValue.get_data() == NULL)
	{
		return false;
	}
	
	// Unserialize value
	try
	{
		CDataStream ssValue((char*)datValue.get_data(), (char*)datValue.get_data() + datValue.get_size(), SER_DISK, CLIENT_VERSION);
		
		ssValue >> value;
	}
	catch (std::exception &e)
	{
		return false;
	}

	// Clear and free memory
	memset(datValue.get_data(), 0, datValue.get_size());
	free(datValue.get_data());
	
	return (ret == 0);
}

template bool CDB::Read<std::pair<std::string, long>, CKeyPool>(const std::pair<std::string, long>&, CKeyPool&);
template bool CDB::Read<std::pair<std::string, long long>, CKeyPool>(const std::pair<std::string, long long>&, CKeyPool&);
template bool CDB::Read<std::pair<std::string, std::string>, CAccount>(const std::pair<std::string, std::string>&, CAccount&);
template bool CDB::Read<std::pair<std::string, ec_point>, CStealthAddress>(const std::pair<std::string, ec_point>&, CStealthAddress&);
template bool CDB::Read<std::string, CBlockLocator>(std::string const&, CBlockLocator&);

template<typename K, typename T>
bool CDB::Write(const K& key, const T& value, bool fOverwrite)
{
	if (!pdb)
	{
		return false;
	}
	
	if (fReadOnly)
	{
		assert(!"Write called on database in read-only mode");
	}
	
	// Key
	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	
	ssKey.reserve(1000);
	ssKey << key;
	
	Dbt datKey(&ssKey[0], ssKey.size());

	// Value
	CDataStream ssValue(SER_DISK, CLIENT_VERSION);
	
	ssValue.reserve(10000);
	ssValue << value;
	
	Dbt datValue(&ssValue[0], ssValue.size());

	// Write
	int ret = pdb->put(activeTxn, &datKey, &datValue, (fOverwrite ? 0 : DB_NOOVERWRITE));

	// Clear memory in case it was a private key
	memset(datKey.get_data(), 0, datKey.get_size());
	memset(datValue.get_data(), 0, datValue.get_size());
	
	return (ret == 0);
}

template bool CDB::Write<std::pair<std::string, long>, CKeyPool>(const std::pair<std::string, long>&, const CKeyPool&, bool);
template bool CDB::Write<std::pair<std::string, long long>, CKeyPool>(const std::pair<std::string, long long>&, const CKeyPool&, bool);
template bool CDB::Write<std::pair<std::string, unsigned int>, CMasterKey>(const std::pair<std::string, unsigned int>&, const CMasterKey&, bool);
template bool CDB::Write<std::pair<std::string, std::string>, std::string>(const std::pair<std::string, std::string>&, const std::string&, bool);
template bool CDB::Write<std::pair<std::string, std::string>, CAccount>(const std::pair<std::string, std::string>&, const CAccount&, bool);
template bool CDB::Write<std::pair<std::string, CPubKey>, CKeyMetadata>(const std::pair<std::string, CPubKey>&,	const CKeyMetadata&, bool);
template bool CDB::Write<std::pair<std::string, CPubKey>, std::pair<CPrivKey, uint256>>(const std::pair<std::string, CPubKey>&, const std::pair<CPrivKey, uint256>&, bool);
template bool CDB::Write<std::pair<std::string, CPubKey>, std::vector<unsigned char>>(const std::pair<std::string, CPubKey>&, const std::vector<unsigned char>&, bool);
template bool CDB::Write<std::pair<std::string, ec_point>, CStealthAddress>(const std::pair<std::string, ec_point>&, const CStealthAddress&, bool);
template bool CDB::Write<std::pair<std::string, CScript>, char>(const std::pair<std::string, CScript>&, const char&, bool);
template bool CDB::Write<std::pair<std::string, CKeyID>, CStealthKeyMetadata>(const std::pair<std::string, CKeyID>&, const CStealthKeyMetadata&, bool);
template bool CDB::Write<std::pair<std::string, uint160>, CScript>(const std::pair<std::string, uint160>&, const CScript&, bool);
template bool CDB::Write<std::pair<std::string, uint256>, CWalletTx>(std::pair<std::string, uint256> const&, const CWalletTx&, bool);
template bool CDB::Write<boost::tuples::tuple<std::string, std::string, unsigned long>, CAccountingEntry>(const boost::tuples::tuple<std::string, std::string, unsigned long>&, const CAccountingEntry&, bool);
template bool CDB::Write<boost::tuples::tuple<std::string, std::string, unsigned long long>, CAccountingEntry>(const boost::tuples::tuple<std::string, std::string, unsigned long long>&, const CAccountingEntry&, bool);
template bool CDB::Write<std::string, long>(const std::string&, const long&, bool);
template bool CDB::Write<std::string, long long>(const std::string&, const long long&, bool);
template bool CDB::Write<std::string, CPubKey>(const std::string&, const CPubKey&, bool);
template bool CDB::Write<std::string, CBlockLocator>(const std::string&, const CBlockLocator&, bool);

template<typename K>
bool CDB::Erase(const K& key)
{
	if (!pdb)
	{
		return false;
	}

	if (fReadOnly)
	{
		assert(!"Erase called on database in read-only mode");
	}
	
	// Key
	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	
	ssKey.reserve(1000);
	ssKey << key;
	
	Dbt datKey(&ssKey[0], ssKey.size());

	// Erase
	int ret = pdb->del(activeTxn, &datKey, 0);

	// Clear memory
	memset(datKey.get_data(), 0, datKey.get_size());
	
	return (ret == 0 || ret == DB_NOTFOUND);
}

template bool CDB::Erase<std::pair<std::string, long>>(const std::pair<std::string, long>&);
template bool CDB::Erase<std::pair<std::string, long long>>(const std::pair<std::string, long long>&);
template bool CDB::Erase<std::pair<std::string, std::string>>(const std::pair<std::string, std::string>&);
template bool CDB::Erase<std::pair<std::string, CScript>>(const std::pair<std::string, CScript>&);
template bool CDB::Erase<std::pair<std::string, CPubKey>>(const std::pair<std::string, CPubKey>&);
template bool CDB::Erase<std::pair<std::string, CKeyID>>(const std::pair<std::string, CKeyID>&);
template bool CDB::Erase<std::pair<std::string, uint256>>(const std::pair<std::string, uint256>&);

template<typename K>
bool CDB::Exists(const K& key)
{
	if (!pdb)
	{
		return false;
	}
	
	// Key
	CDataStream ssKey(SER_DISK, CLIENT_VERSION);
	
	ssKey.reserve(1000);
	ssKey << key;
	
	Dbt datKey(&ssKey[0], ssKey.size());

	// Exists
	int ret = pdb->exists(activeTxn, &datKey, 0);

	// Clear memory
	memset(datKey.get_data(), 0, datKey.get_size());
	
	return (ret == 0);
}

Dbc* CDB::GetCursor()
{
	if (!pdb)
	{
		return NULL;
	}
	
	Dbc* pcursor = NULL;
	int ret = pdb->cursor(NULL, &pcursor, 0);
	
	if (ret != 0)
	{
		return NULL;
	}
	
	return pcursor;
}

int CDB::ReadAtCursor(Dbc* pcursor, CDataStream& ssKey, CDataStream& ssValue, unsigned int fFlags)
{
	// Read at cursor
	Dbt datKey;
	
	if (fFlags == DB_SET || fFlags == DB_SET_RANGE || fFlags == DB_GET_BOTH || fFlags == DB_GET_BOTH_RANGE)
	{
		datKey.set_data(&ssKey[0]);
		datKey.set_size(ssKey.size());
	}
	
	Dbt datValue;
	
	if (fFlags == DB_GET_BOTH || fFlags == DB_GET_BOTH_RANGE)
	{
		datValue.set_data(&ssValue[0]);
		datValue.set_size(ssValue.size());
	}
	
	datKey.set_flags(DB_DBT_MALLOC);
	datValue.set_flags(DB_DBT_MALLOC);
	
	int ret = pcursor->get(&datKey, &datValue, fFlags);
	
	if (ret != 0)
	{
		return ret;
	}
	else if (datKey.get_data() == NULL || datValue.get_data() == NULL)
	{
		return 99999;
	}
	
	// Convert to streams
	ssKey.SetType(SER_DISK);
	ssKey.clear();
	ssKey.write((char*)datKey.get_data(), datKey.get_size());
	
	ssValue.SetType(SER_DISK);
	ssValue.clear();
	ssValue.write((char*)datValue.get_data(), datValue.get_size());

	// Clear and free memory
	memset(datKey.get_data(), 0, datKey.get_size());
	memset(datValue.get_data(), 0, datValue.get_size());
	
	free(datKey.get_data());
	free(datValue.get_data());
	
	return 0;
}

bool CDB::TxnBegin()
{
	if (!pdb || activeTxn)
	{
		return false;
	}
	
	DbTxn* ptxn = bitdb.TxnBegin();
	
	if (!ptxn)
	{
		return false;
	}
	
	activeTxn = ptxn;
	
	return true;
}

bool CDB::TxnCommit()
{
	if (!pdb || !activeTxn)
	{
		return false;
	}
	
	int ret = activeTxn->commit(0);
	
	activeTxn = NULL;
	
	return (ret == 0);
}

bool CDB::TxnAbort()
{
	if (!pdb || !activeTxn)
	{
		return false;
	}
	
	int ret = activeTxn->abort();
	
	activeTxn = NULL;
	
	return (ret == 0);
}

bool CDB::ReadVersion(int& nVersion)
{
	nVersion = 0;
	
	return Read(std::string("version"), nVersion);
}

bool CDB::WriteVersion(int nVersion)
{
	return Write(std::string("version"), nVersion);
}

bool CDB::Rewrite(const std::string& strFile, const char* pszSkip)
{
    while (true)
    {
        {
            LOCK(bitdb.cs_db);
            
			if (!bitdb.mapFileUseCount.count(strFile) || bitdb.mapFileUseCount[strFile] == 0)
            {
                // Flush log data to the dat file
                bitdb.CloseDb(strFile);
                bitdb.CheckpointLSN(strFile);
                bitdb.mapFileUseCount.erase(strFile);

                bool fSuccess = true;
                
				LogPrintf("Rewriting %s...\n", strFile);
                
				std::string strFileRes = strFile + ".rewrite";
				
                { // surround usage of db with extra {}
                    CDB db(strFile.c_str(), "r");
                    Db* pdbCopy = new Db(&bitdb.dbenv, 0);

                    int ret = pdbCopy->open(NULL,                 // Txn pointer
                                            strFileRes.c_str(),   // Filename
                                            "main",    // Logical db name
                                            DB_BTREE,  // Database type
                                            DB_CREATE,    // Flags
                                            0);
                    
					if (ret > 0)
                    {
                        LogPrintf("Cannot create database file %s\n", strFileRes);
                        
						fSuccess = false;
                    }

                    Dbc* pcursor = db.GetCursor();
                    
					if (pcursor)
					{
                        while (fSuccess)
                        {
                            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
                            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
                            
							int ret = db.ReadAtCursor(pcursor, ssKey, ssValue, DB_NEXT);
                            
							if (ret == DB_NOTFOUND)
                            {
                                pcursor->close();
                                
								break;
                            }
                            else if (ret != 0)
                            {
                                pcursor->close();
                                
								fSuccess = false;
                                
								break;
                            }
							
                            if (pszSkip && strncmp(&ssKey[0], pszSkip, std::min(ssKey.size(), strlen(pszSkip))) == 0)
							{
                                continue;
                            }
							
							if (strncmp(&ssKey[0], "\x07version", 8) == 0)
                            {
                                // Update version:
                                ssValue.clear();
                                ssValue << CLIENT_VERSION;
                            }
                            
							Dbt datKey(&ssKey[0], ssKey.size());
                            Dbt datValue(&ssValue[0], ssValue.size());
                            int ret2 = pdbCopy->put(NULL, &datKey, &datValue, DB_NOOVERWRITE);
                            
							if (ret2 > 0)
							{
                                fSuccess = false;
							}
                        }
                    }
					
					if (fSuccess)
                    {
                        db.Close();
                        bitdb.CloseDb(strFile);
                        
						if (pdbCopy->close(0))
						{
                            fSuccess = false;
                        }
						
						delete pdbCopy;
                    }
                }
				
                if (fSuccess)
                {
                    Db dbA(&bitdb.dbenv, 0);
                    
					if (dbA.remove(strFile.c_str(), NULL, 0))
					{
                        fSuccess = false;
                    }
					
					Db dbB(&bitdb.dbenv, 0);
                    
					if (dbB.rename(strFileRes.c_str(), NULL, strFile.c_str(), 0))
					{
                        fSuccess = false;
					}
                }
                
				if (!fSuccess)
				{
                    LogPrintf("Rewriting of %s FAILED!\n", strFileRes);
                }
				
				return fSuccess;
            }
        }
		
        MilliSleep(100);
    }
	
    return false;
}


