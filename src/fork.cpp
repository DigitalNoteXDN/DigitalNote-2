#include "main_extern.h"
#include "cblockindex.h"

#include "fork.h"

std::string getDevelopersAdress(const CBlockIndex* pindex)
{
	if(pindex->GetBlockTime() < VERION_1_0_1_5_MANDATORY_UPDATE_START)
	{
		return VERION_1_0_0_0_DEVELOPER_ADDRESS;
	}
	else if(pindex->nHeight <= VERION_1_0_4_2_MANDATORY_UPDATE_BLOCK)
	{
		return VERION_1_0_1_5_DEVELOPER_ADDRESS;
	}
	
	return VERION_1_0_4_2_DEVELOPER_ADDRESS;
}

