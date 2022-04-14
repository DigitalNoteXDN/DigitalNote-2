#ifndef CDB_H
#define CDB_H

#include <db_cxx.h>

class CDataStream;

/** RAII class that provides access to a Berkeley database */
class CDB
{
protected:
	Db* pdb;
	std::string strFile;
	DbTxn *activeTxn;
	bool fReadOnly;

	explicit CDB(const std::string& strFilename, const char* pszMode="r+");
	~CDB();

public:
	void Close();

private:
	CDB(const CDB&);
	void operator=(const CDB&);

protected:
	template<typename K, typename T>
	bool Read(const K& key, T& value);

	template<typename K, typename T>
	bool Write(const K& key, const T& value, bool fOverwrite=true);

	template<typename K>
	bool Erase(const K& key);

	template<typename K>
	bool Exists(const K& key);

	Dbc* GetCursor();
	int ReadAtCursor(Dbc* pcursor, CDataStream& ssKey, CDataStream& ssValue, unsigned int fFlags=DB_NEXT);

public:
	bool TxnBegin();
	bool TxnCommit();
	bool TxnAbort();
	bool ReadVersion(int& nVersion);
	bool WriteVersion(int nVersion);

	bool static Rewrite(const std::string& strFile, const char* pszSkip = NULL);
};

#endif // CDB_H
