#include <boost/filesystem.hpp>

#include "util.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "hash.h"
#include "cmasternodeman.h"
#include "cautofile.h"
#include "cdatastream.h"
#include "cflatdata.h"
#include "serialize.h"

#include "cmasternodedb.h"

//
// CMasternodeDB
//
CMasternodeDB::CMasternodeDB()
{
	pathMN = GetDataDir() / "mncache.dat";
	strMagicMessage = "MasternodeCache";
}

bool CMasternodeDB::Write(const CMasternodeMan& mnodemanToSave)
{
	int64_t nStart = GetTimeMillis();

	// serialize addresses, checksum data up to that point, then append csum
	CDataStream ssMasternodes(SER_DISK, CLIENT_VERSION);
	ssMasternodes << strMagicMessage; // masternode cache file specific magic message
	ssMasternodes << FLATDATA(Params().MessageStart()); // network specific magic number
	ssMasternodes << mnodemanToSave;
	uint256 hash = Hash(ssMasternodes.begin(), ssMasternodes.end());
	ssMasternodes << hash;

	// open output file, and associate with CAutoFile
	FILE *file = fopen(pathMN.string().c_str(), "wb");
	CAutoFile fileout = CAutoFile(file, SER_DISK, CLIENT_VERSION);

	if (!fileout)
	{
		return error("%s : Failed to open file %s", __func__, pathMN.string());
	}

	// Write and commit header, data
	try
	{
		fileout << ssMasternodes;
	}
	catch (std::exception &e)
	{
		return error("%s : Serialize or I/O error - %s", __func__, e.what());
	}

	FileCommit(fileout);
	fileout.fclose();

	LogPrintf("Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
	LogPrintf("  %s\n", mnodemanToSave.ToString());

	return true;
}

CMasternodeDB::ReadResult CMasternodeDB::Read(CMasternodeMan& mnodemanToLoad)
{
	int64_t nStart = GetTimeMillis();

	// open input file, and associate with CAutoFile
	FILE *file = fopen(pathMN.string().c_str(), "rb");
	CAutoFile filein = CAutoFile(file, SER_DISK, CLIENT_VERSION);

	if (!filein)
	{
		error("%s : Failed to open file %s", __func__, pathMN.string());
		
		return FileError;
	}

	// use file size to size memory buffer
	int fileSize = boost::filesystem::file_size(pathMN);
	int dataSize = fileSize - sizeof(uint256);

	// Don't try to resize to a negative number if file is small
	if (dataSize < 0)
	{
		dataSize = 0;
	}

	std::vector<unsigned char> vchData;
	vchData.resize(dataSize);
	uint256 hashIn;

	// read data and checksum from file
	try
	{
		filein.read((char *)&vchData[0], dataSize);
		filein >> hashIn;
	}
	catch (std::exception &e)
	{
		error("%s : Deserialize or I/O error - %s", __func__, e.what());
		
		return HashReadError;
	}

	filein.fclose();

	CDataStream ssMasternodes(vchData, SER_DISK, CLIENT_VERSION);

	// verify stored checksum matches input data
	uint256 hashTmp = Hash(ssMasternodes.begin(), ssMasternodes.end());

	if (hashIn != hashTmp)
	{
		error("%s : Checksum mismatch, data corrupted", __func__);
		
		return IncorrectHash;
	}

	unsigned char pchMsgTmp[4];
	std::string strMagicMessageTmp;

	try
	{
		// de-serialize file header (masternode cache file specific magic message) and ..
		ssMasternodes >> strMagicMessageTmp;

		// ... verify the message matches predefined one
		if (strMagicMessage != strMagicMessageTmp)
		{
			error("%s : Invalid masternode cache magic message", __func__);
			
			return IncorrectMagicMessage;
		}

		// de-serialize file header (network specific magic number) and ..
		ssMasternodes >> FLATDATA(pchMsgTmp);

		// ... verify the network matches ours
		if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
		{
			error("%s : Invalid network magic number", __func__);
			
			return IncorrectMagicNumber;
		}

		// de-serialize address data into one CMnList object
		ssMasternodes >> mnodemanToLoad;
	}
	catch (std::exception &e)
	{
		mnodemanToLoad.Clear();
		
		error("%s : Deserialize or I/O error - %s", __func__, e.what());
		
		return IncorrectFormat;
	}

	mnodemanToLoad.CheckAndRemove(); // clean out expired

	LogPrintf("Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
	LogPrintf("  %s\n", mnodemanToLoad.ToString());

	return Ok;
}

