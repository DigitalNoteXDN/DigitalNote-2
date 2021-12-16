#include "types/ccriticalsection.h"
#include "uint/uint256.h"
#include "masternodeman.h"
#include "main_extern.h"
#include "cblockindex.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "cmasternodepayments.h"
#include "cmasternodepaymentwinner.h"

#include "masternode.h"

CCriticalSection cs_masternodes;
// keep track of the scanning errors I've seen
std::map<uint256, int> mapSeenMasternodeScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

CMasternodeMan mnodeman;

CCriticalSection cs_masternodepayments;
/** Object for who's going to get paid on which blocks */
CMasternodePayments masternodePayments;
// keep track of Masternode votes I've seen
std::map<uint256, CMasternodePaymentWinner> mapSeenMasternodeVotes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
	if (pindexBest == NULL)
	{
		return false;
	}

	if(nBlockHeight == 0)
	{
		nBlockHeight = pindexBest->nHeight;
	}

	if(mapCacheBlockHashes.count(nBlockHeight))
	{	
		hash = mapCacheBlockHashes[nBlockHeight];
		
		return true;
	}

	const CBlockIndex *BlockLastSolved = pindexBest;
	const CBlockIndex *BlockReading = pindexBest;

	if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || pindexBest->nHeight+1 < nBlockHeight)
	{
		return false;
	}

	int nBlocksAgo = 0;
	if(nBlockHeight > 0)
	{
		nBlocksAgo = (pindexBest->nHeight+1)-nBlockHeight;
	}

	assert(nBlocksAgo >= 0);

	int n = 0;
	for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++)
	{
		if(n >= nBlocksAgo)
		{
			hash = BlockReading->GetBlockHash();
			mapCacheBlockHashes[nBlockHeight] = hash;
			
			return true;
		}
		
		n++;

		if (BlockReading->pprev == NULL)
		{
			assert(BlockReading);
			break;
		}
		
		BlockReading = BlockReading->pprev;
	}

	return false;
}

