#ifndef MASTERNODEDB_H
#define MASTERNODEDB_H

#include <boost/filesystem/path.hpp>

class CMasternodeMan;

/** Access to the MN database (mncache.dat) */
class CMasternodeDB
{
private:
	boost::filesystem::path pathMN;
	std::string strMagicMessage;

public:
	enum ReadResult
	{
		Ok,
		FileError,
		HashReadError,
		IncorrectHash,
		IncorrectMagicMessage,
		IncorrectMagicNumber,
		IncorrectFormat
	};

	CMasternodeDB();

	bool Write(const CMasternodeMan &mnodemanToSave);
	ReadResult Read(CMasternodeMan& mnodemanToLoad);
};

#endif // MASTERNODEDB_H
