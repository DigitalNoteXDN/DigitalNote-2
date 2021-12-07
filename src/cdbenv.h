#ifndef CDBENV_H
#define CDBENV_H

#include <boost/filesystem.hpp>
#include <db_cxx.h>

#include "types/ccriticalsection.h"

class CDBEnv
{
private:
    bool fDbEnvInit;
    bool fMockDb;
    boost::filesystem::path pathEnv;
    std::string strPath;

    void EnvShutdown();

public:
    mutable CCriticalSection cs_db;
    DbEnv dbenv;
    std::map<std::string, int> mapFileUseCount;
    std::map<std::string, Db*> mapDb;

    CDBEnv();
    ~CDBEnv();
    void MakeMock();
    bool IsMock();

    /*
     * Verify that database file strFile is OK. If it is not,
     * call the callback to try to recover.
     * This must be called BEFORE strFile is opened.
     * Returns true if strFile is OK.
     */
    enum VerifyResult
	{
		VERIFY_OK,
		RECOVER_OK,
		RECOVER_FAIL
	};
	
    VerifyResult Verify(std::string strFile, bool (*recoverFunc)(CDBEnv& dbenv, std::string strFile));
    /*
     * Salvage data from a file that Verify says is bad.
     * fAggressive sets the DB_AGGRESSIVE flag (see berkeley DB->verify() method documentation).
     * Appends binary key/value pairs to vResult, returns true if successful.
     * NOTE: reads the entire database into memory, so cannot be used
     * for huge databases.
     */
    typedef std::pair<std::vector<unsigned char>, std::vector<unsigned char> > KeyValPair;
    bool Salvage(std::string strFile, bool fAggressive, std::vector<KeyValPair>& vResult);

    bool Open(boost::filesystem::path pathEnv_);
    void Close();
    void Flush(bool fShutdown);
    void CheckpointLSN(const std::string& strFile);

    void CloseDb(const std::string& strFile);
    bool RemoveDb(const std::string& strFile);

    DbTxn *TxnBegin(int flags=DB_TXN_WRITE_NOSYNC);
};

#endif // CDBENV_H
