#include "compat.h"

#include "util.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "net/cbanentry.h"
#include "hash.h"
#include "net/csubnet.h"
#include "cautofile.h"
#include "cdatastream.h"
#include "cflatdata.h"
#include "serialize.h"

#include "net/cbandb.h"

CBanDB::CBanDB()
{
	pathBanlist = GetDataDir() / "banlist.dat";
}

bool CBanDB::Write(const banmap_t& banSet)
{
	// Generate random temporary filename
	unsigned short randv = 0;
	GetRandBytes((unsigned char*)&randv, sizeof(randv));
	std::string tmpfn = strprintf("banlist.dat.%04x", randv);

	// serialize banlist, checksum data up to that point, then append csum
	CDataStream ssBanlist(SER_DISK, CLIENT_VERSION);
	ssBanlist << FLATDATA(Params().MessageStart());
	ssBanlist << banSet;
	
	uint256 hash = Hash(ssBanlist.begin(), ssBanlist.end());
	ssBanlist << hash;

	// open temp output file, and associate with CAutoFile
	boost::filesystem::path pathTmp = GetDataDir() / tmpfn;
	FILE *file = fopen(pathTmp.string().c_str(), "wb");
	CAutoFile fileout = CAutoFile(file, SER_DISK, CLIENT_VERSION);
	
	if (!fileout)
	{
		return error("%s: Failed to open file %s", __func__, pathTmp.string());
	}
	
	// Write and commit header, data
	try
	{
		fileout << ssBanlist;
	}
	catch (const std::exception& e)
	{
		return error("%s: Serialize or I/O error - %s", __func__, e.what());
	}
	
	FileCommit(fileout);
	fileout.fclose();

	// replace existing banlist.dat, if any, with new banlist.dat.XXXX
	if (!RenameOver(pathTmp, pathBanlist))
	{
		return error("%s: Rename-into-place failed", __func__);
	}
	
	return true;
}

bool CBanDB::Read(banmap_t& banSet)
{
	// open input file, and associate with CAutoFile
	FILE *file = fopen(pathBanlist.string().c_str(), "rb");
	CAutoFile filein = CAutoFile(file, SER_DISK, CLIENT_VERSION);

	if (!filein)
	{
		return error("%s: Failed to open file %s", __func__, pathBanlist.string());
	}

	// use file size to size memory buffer
	uint64_t fileSize = boost::filesystem::file_size(pathBanlist);
	uint64_t dataSize = 0;

	// Don't try to resize to a negative number if file is small
	if (fileSize >= sizeof(uint256))
	{
		dataSize = fileSize - sizeof(uint256);
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
	catch (const std::exception& e)
	{
		return error("%s: Deserialize or I/O error - %s", __func__, e.what());
	}

	filein.fclose();

	CDataStream ssBanlist(vchData, SER_DISK, CLIENT_VERSION);

	// verify stored checksum matches input data
	uint256 hashTmp = Hash(ssBanlist.begin(), ssBanlist.end());

	if (hashIn != hashTmp)
	{
		return error("%s: Checksum mismatch, data corrupted", __func__);
	}

	unsigned char pchMsgTmp[4];
	try
	{
		// de-serialize file header (network specific magic number) and ..
		ssBanlist >> FLATDATA(pchMsgTmp);

		// ... verify the network matches ours
		if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
		{
			return error("%s: Invalid network magic number", __func__);
		}
		
		// de-serialize address data into one CAddrMan object
		ssBanlist >> banSet;
	}
	catch (const std::exception& e)
	{
		return error("%s: Deserialize or I/O error - %s", __func__, e.what());
	}

	return true;
}

