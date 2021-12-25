#include "compat.h"

#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include "uint/uint256.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "main_extern.h"
#include "cblockindex.h"
#include "util/backwards.h"

#include "checkpoints.h"

static const int nCheckpointSpan = 5000;

namespace Checkpoints
{
	//
	// What makes a good checkpoint block?
	// + Is surrounded by blocks with reasonable timestamps
	//   (no blocks before with a timestamp after, none after with
	//    timestamp before)
	// + Contains no strange transactions
	//
	static MapCheckpoints mapCheckpoints =
		boost::assign::map_list_of
		(0,			Params().HashGenesisBlock() )
		(1,			uint256("0x0000246c37061884d4f9a390cf0db25cca1525a63f01f906367d5af1c409a8aa"))
		(2,			uint256("0x00002e010d313f7f4dd57f5d543ea8326948b0bf6b39802305f63ff8ab26c998"))
		(125,		uint256("0x3190aa97b492f9e018cdec5e88c9be46eca8f57bfc3d51c68834bbed2c6a2afd"))
		(170,		uint256("0x00012035bc36a99eba234cf6968fd7457a5b8755d20df915d37d67540a06e95d"))
		(45000,		uint256("0x5308f77f44bffa63eb271e63fe003422d593930a92a9f5dd1ae7de5327a3218b"))
		(47500,		uint256("0x0cb5289c555684443f303fcc7d1e8608732bdf48d90bca5a14a9b0c709066078"))
		(47700,		uint256("0xf78d6c29f621194e786b4c470278663671c285993869a2d0d59bba31425c9a02"))
		(65544,		uint256("0x000000000049a58f04f1beb0ed6c2e6ab11919b8a4323caee6e9a217c89eff69"))
		(66400,		uint256("0x8035935fc3b5e8b58ab023336d6f7f1ec35eac9930cdd238ce35771cd7b3964d"))
		(67500,		uint256("0x4df36f82141ce789aa64d80908aafca145d09f5257ebb3b7550f94e2624a2d98"))
		(68200,		uint256("0x000000000005ab4fb2fec8705c51aee6b04ebf51f98bca11e61d7f41bcc51e92"))
		(190900,	uint256("0x00000000000324d80ae543f7b4882de88a6711c644dfdc596fbaab3225db859e"))
		(394624,	uint256("b426f7eeaaf3450ef78c6a8716665d3ed0ba5669ad40fb9d9497a29a088c9faf"))
		(461976,	uint256("000000000004ef0d42a98d0ee3c5bd4b1ec787c91c4f57cfe3b5d179eaea0d13"));

	// TestNet has no checkpoints
	static MapCheckpoints mapCheckpointsTestnet;

	bool CheckHardened(int nHeight, const uint256& hash)
	{
		MapCheckpoints& checkpoints = (TestNet() ? mapCheckpointsTestnet : mapCheckpoints);
		MapCheckpoints::const_iterator i = checkpoints.find(nHeight);
		
		if (i == checkpoints.end())
		{
			return true;
		}
		
		return hash == i->second;
	}

	int GetTotalBlocksEstimate()
	{
		MapCheckpoints& checkpoints = (TestNet() ? mapCheckpointsTestnet : mapCheckpoints);

		if (checkpoints.empty())
		{
			return 0;
		}
		
		return checkpoints.rbegin()->first;
	}

	CBlockIndex* GetLastCheckpoint(const std::map<uint256, CBlockIndex*>& mapBlockIndex)
	{
		MapCheckpoints& checkpoints = (TestNet() ? mapCheckpointsTestnet : mapCheckpoints);

		for(const MapCheckpoints::value_type& i : backwards<MapCheckpoints>(checkpoints))
		{
			const uint256& hash = i.second;
			std::map<uint256, CBlockIndex*>::const_iterator t = mapBlockIndex.find(hash);
			
			if (t != mapBlockIndex.end())
			{
				return t->second;
			}
		}
		
		return NULL;
	}

	// Automatically select a suitable sync-checkpoint
	const CBlockIndex* AutoSelectSyncCheckpoint()
	{
		const CBlockIndex *pindex = pindexBest;
		
		// Search backward for a block within max span and maturity window
		while (pindex->pprev && pindex->nHeight + nCheckpointSpan > pindexBest->nHeight)
		{
			pindex = pindex->pprev;
		}
		
		return pindex;
	}

	// Check against synchronized checkpoint
	bool CheckSync(int nHeight)
	{
		const CBlockIndex* pindexSync = AutoSelectSyncCheckpoint();
		
		if (nHeight <= pindexSync->nHeight)
		{
			return false;
		}
		
		return true;
	}
}

