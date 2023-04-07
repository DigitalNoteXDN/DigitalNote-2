#include "compat.h"

#include "main.h"
#include "main_extern.h"
#include "util.h"
#include "thread.h"
#include "net/cnode.h"
#include "cblockindex.h"
#include "cmasternodepayments.h"
#include "cmasternodepaymentwinner.h"
#include "masternode_extern.h"
#include "cmnenginepool.h"
#include "mnengine_extern.h"
#include "script.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"

#include "masternode-payments.h"

void ProcessMessageMasternodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
	if(!mnEnginePool.IsBlockchainSynced())
	{
		return;
	}

	if (strCommand == "mnget") //Masternode Payments Request Sync
	{
		if(pfrom->HasFulfilledRequest("mnget"))
		{
			LogPrintf("mnget - peer already asked me for the list\n");
			
			Misbehaving(pfrom->GetId(), 20);
			
			return;
		}

		pfrom->FulfilledRequest("mnget");
		masternodePayments.Sync(pfrom);
		
		LogPrintf("mnget - Sent Masternode winners to %s\n", pfrom->addr.ToString().c_str());
	}
	else if (strCommand == "mnw") //Masternode Payments Declare Winner
	{
		LOCK(cs_masternodepayments);

		//this is required in litemode
		CMasternodePaymentWinner winner;
		
		vRecv >> winner;

		if(pindexBest == NULL)
		{
			return;
		}
		
		CTxDestination address1;
		ExtractDestination(winner.payee, address1);
		CDigitalNoteAddress address2(address1);

		uint256 hash = winner.GetHash();
		
		if(mapSeenMasternodeVotes.count(hash))
		{
			if(fDebug)
			{
				LogPrintf(
					"mnw - seen vote %s Addr %s Height %d bestHeight %d\n",
					hash.ToString().c_str(),
					address2.ToString().c_str(),
					winner.nBlockHeight,
					pindexBest->nHeight
				);
			}
			
			return;
		}

		if(winner.nBlockHeight < pindexBest->nHeight - 10 || winner.nBlockHeight > pindexBest->nHeight+20)
		{
			LogPrintf(
				"mnw - winner out of range %s Addr %s Height %d bestHeight %d\n",
				winner.vin.ToString().c_str(),
				address2.ToString().c_str(),
				winner.nBlockHeight,
				pindexBest->nHeight
			);
			
			return;
		}

		if(winner.vin.nSequence != std::numeric_limits<unsigned int>::max())
		{
			LogPrintf("mnw - invalid nSequence\n");
			
			Misbehaving(pfrom->GetId(), 100);
			
			return;
		}

		LogPrintf(
			"mnw - winning vote - Vin %s Addr %s Height %d bestHeight %d\n",
			winner.vin.ToString().c_str(),
			address2.ToString().c_str(),
			winner.nBlockHeight,
			pindexBest->nHeight
		);

		if(!masternodePayments.CheckSignature(winner))
		{
			LogPrintf("mnw - invalid signature\n");
			
			Misbehaving(pfrom->GetId(), 100);
			
			return;
		}

		mapSeenMasternodeVotes.insert(std::make_pair(hash, winner));

		if(masternodePayments.AddWinningMasternode(winner))
		{
			masternodePayments.Relay(winner);
		}
	}
}

