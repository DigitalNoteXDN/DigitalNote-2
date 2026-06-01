#include "main_extern.h"
#include "cblockindex.h"
#include "chainparams.h"

#include "fork.h"

std::string getDevelopersAdress(const CBlockIndex* pindex)
{
	// Testnet uses a single fixed developer address from inception.
	// Mainnet picks between three legacy addresses based on height/time.
	if(TestNet())
	{
		return TESTNET_DEVELOPER_ADDRESS;
	}

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

