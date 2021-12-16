#include <stdint.h>

#include "util.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "cmasternodedb.h"
#include "masternode_extern.h"

#include "masternodeman.h"

void DumpMasternodes()
{
	int64_t nStart = GetTimeMillis();

	CMasternodeDB mndb;
	CMasternodeMan tempMnodeman;

	LogPrintf("Verifying mncache.dat format...\n");

	CMasternodeDB::ReadResult readResult = mndb.Read(tempMnodeman);

	// there was an error and it was not an error on file openning => do not proceed
	if (readResult == CMasternodeDB::FileError)
	{
		LogPrintf("Missing masternode list file - mncache.dat, will try to recreate\n");
	}
	else if (readResult != CMasternodeDB::Ok)
	{
		LogPrintf("Error reading mncache.dat: ");
		
		if(readResult == CMasternodeDB::IncorrectFormat)
		{
			LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
		}
		else
		{
			LogPrintf("file format is unknown or invalid, please fix it manually\n");
			
			return;
		}
	}

	LogPrintf("Writting info to mncache.dat...\n");
	
	mndb.Write(mnodeman);

	LogPrintf("Masternode dump finished  %dms\n", GetTimeMillis() - nStart);
}

