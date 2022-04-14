#include "compat.h"

#include "util.h"
#include "cblockindex.h"
#include "csporkmanager.h"
#include "csporkmessage.h"
#include "main_extern.h"
#include "net/cnode.h"
#include "main.h"

#include "spork.h"

CSporkManager sporkManager;

std::map<uint256, CSporkMessage> mapSporks;
std::map<int, CSporkMessage> mapSporksActive;

void ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
	if(fLiteMode)
	{
		return; //disable all mnengine/masternode related functionality
	}
	
	if (strCommand == "spork")
	{
		//LogPrintf("ProcessSpork::spork\n");
		CDataStream vMsg(vRecv);
		CSporkMessage spork;
		vRecv >> spork;

		if(pindexBest == NULL)
		{
			return;
		}
		
		uint256 hash = spork.GetHash();
		
		if(mapSporksActive.count(spork.nSporkID))
		{
			if(mapSporksActive[spork.nSporkID].nTimeSigned >= spork.nTimeSigned)
			{
				if(fDebug)
				{
					LogPrintf("spork - seen %s block %d \n", hash.ToString().c_str(), pindexBest->nHeight);
				}
				
				return;
			}
			else
			{
				if(fDebug)
				{
					LogPrintf("spork - got updated spork %s block %d \n", hash.ToString().c_str(), pindexBest->nHeight);
				}
			}
		}

		LogPrintf("spork - new %s ID %d Time %d bestHeight %d\n", hash.ToString().c_str(), spork.nSporkID, spork.nValue, pindexBest->nHeight);

		if(!sporkManager.CheckSignature(spork))
		{
			LogPrintf("spork - invalid signature\n");
			
			Misbehaving(pfrom->GetId(), 100);
			
			return;
		}

		mapSporks[hash] = spork;
		mapSporksActive[spork.nSporkID] = spork;
		sporkManager.Relay(spork);

		//does a task if needed
		ExecuteSpork(spork.nSporkID, spork.nValue);
	}
	
	if (strCommand == "getsporks")
	{
		std::map<int, CSporkMessage>::iterator it = mapSporksActive.begin();

		while(it != mapSporksActive.end())
		{
			pfrom->PushMessage("spork", it->second);
			it++;
		}
	}
}

// grab the spork, otherwise say it's off
bool IsSporkActive(int nSporkID)
{
	int64_t r = -1;

	if(mapSporksActive.count(nSporkID))
	{
		r = mapSporksActive[nSporkID].nValue;
	}
	else
	{
		if(nSporkID == SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT) r = SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT_DEFAULT;
		if(nSporkID == SPORK_2_INSTANTX) r = SPORK_2_INSTANTX_DEFAULT;
		if(nSporkID == SPORK_3_INSTANTX_BLOCK_FILTERING) r = SPORK_3_INSTANTX_BLOCK_FILTERING_DEFAULT;
		if(nSporkID == SPORK_5_MAX_VALUE) r = SPORK_5_MAX_VALUE_DEFAULT;
		if(nSporkID == SPORK_6_REPLAY_BLOCKS) r = SPORK_6_REPLAY_BLOCKS_DEFAULT;
		if(nSporkID == SPORK_7_MASTERNODE_SCANNING) r = SPORK_7_MASTERNODE_SCANNING;
		if(nSporkID == SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT) r = SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT;
		if(nSporkID == SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT) r = SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT_DEFAULT;
		if(nSporkID == SPORK_10_MASTERNODE_PAY_UPDATED_NODES) r = SPORK_10_MASTERNODE_PAY_UPDATED_NODES_DEFAULT;
		if(nSporkID == SPORK_11_RESET_BUDGET) r = SPORK_11_RESET_BUDGET_DEFAULT;
		if(nSporkID == SPORK_12_RECONSIDER_BLOCKS) r = SPORK_12_RECONSIDER_BLOCKS_DEFAULT;
		if(nSporkID == SPORK_13_ENABLE_SUPERBLOCKS) r = SPORK_13_ENABLE_SUPERBLOCKS_DEFAULT;

		if(r == -1)
		{
			LogPrintf("GetSpork::Unknown Spork %d\n", nSporkID);
		}
	}
	
	if(r == -1)
	{
		r = 4070908800; //return 2099-1-1 by default
	}

	return r < GetTime();
}

// grab the value of the spork on the network, or the default
int64_t GetSporkValue(int nSporkID)
{
	int64_t r = -1;

	if(mapSporksActive.count(nSporkID))
	{
		r = mapSporksActive[nSporkID].nValue;
	}
	else
	{
		if(nSporkID == SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT) r = SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT_DEFAULT;
		if(nSporkID == SPORK_2_INSTANTX) r = SPORK_2_INSTANTX_DEFAULT;
		if(nSporkID == SPORK_3_INSTANTX_BLOCK_FILTERING) r = SPORK_3_INSTANTX_BLOCK_FILTERING_DEFAULT;
		if(nSporkID == SPORK_5_MAX_VALUE) r = SPORK_5_MAX_VALUE_DEFAULT;
		if(nSporkID == SPORK_6_REPLAY_BLOCKS) r = SPORK_6_REPLAY_BLOCKS_DEFAULT;
		if(nSporkID == SPORK_7_MASTERNODE_SCANNING) r = SPORK_7_MASTERNODE_SCANNING;
		if(nSporkID == SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT) r = SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT;
		if(nSporkID == SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT) r = SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT_DEFAULT;
		if(nSporkID == SPORK_10_MASTERNODE_PAY_UPDATED_NODES) r = SPORK_10_MASTERNODE_PAY_UPDATED_NODES_DEFAULT;
		if(nSporkID == SPORK_11_RESET_BUDGET) r = SPORK_11_RESET_BUDGET_DEFAULT;
		if(nSporkID == SPORK_12_RECONSIDER_BLOCKS) r = SPORK_12_RECONSIDER_BLOCKS_DEFAULT;
		if(nSporkID == SPORK_13_ENABLE_SUPERBLOCKS) r = SPORK_13_ENABLE_SUPERBLOCKS_DEFAULT;

		if(r == -1)
		{
			LogPrintf("GetSpork::Unknown Spork %d\n", nSporkID);
		}
	}

	return r;
}

void ExecuteSpork(int nSporkID, int nValue)
{
	
}

/*void ReprocessBlocks(int nBlocks)
{
	std::map<uint256, int64_t>::iterator it = mapRejectedBlocks.begin();
	
	while(it != mapRejectedBlocks.end()){
		//use a window twice as large as is usual for the nBlocks we want to reset
		if((*it).second  > GetTime() - (nBlocks*60*5)) {
			BlockMap::iterator mi = mapBlockIndex.find((*it).first);
			if (mi != mapBlockIndex.end() && (*mi).second) {
				LOCK(cs_main);

				CBlockIndex* pindex = (*mi).second;
				LogPrintf("ReprocessBlocks - %s\n", (*it).first.ToString());

				CValidationState state;
				ReconsiderBlock(state, pindex);
			}
		}
		++it;
	}

	CValidationState state;
	{
		LOCK(cs_main);
		DisconnectBlocksAndReprocess(nBlocks);
	}

	if (state.IsValid()) {
		ActivateBestChain(state);
	}
}*/

