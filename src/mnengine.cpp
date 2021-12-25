#include "compat.h"

#include <algorithm>
#include <random>
#include <openssl/rand.h>

#include "init.h"
#include "util.h"
#include "instantx.h"
#include "cvalidationstate.h"
#include "cwallet.h"
#include "cwallettx.h"
#include "mining.h"
#include "creservekey.h"
#include "net/cnode.h"
#include "net.h"
#include "chashwriter.h"
#include "ckey.h"
#include "cinv.h"
#include "main_extern.h"
#include "cblockindex.h"
#include "cactivemasternode.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "cmasternodepayments.h"
#include "masternode.h"
#include "masternodeman.h"
#include "masternode_extern.h"
#include "ctxdsin.h"
#include "ctxdsout.h"
#include "cmnengineentry.h"
#include "cmnenginequeue.h"
#include "cmnenginebroadcasttx.h"
#include "cmnenginesigner.h"
#include "cmnenginepool.h"
#include "mnengine_extern.h"

#include "mnengine.h"

// The main object for accessing mnengine
CMNenginePool mnEnginePool;
// A helper object for signing messages from Masternodes
CMNengineSigner mnEngineSigner;
// The current mnengines in progress on the network
std::vector<CMNengineQueue> vecMNengineQueue;
// keep track of the scanning errors I've seen
std::map<uint256, CMNengineBroadcastTx> mapMNengineBroadcastTxes;
// Keep track of the active Masternode
CActiveMasternode activeMasternode;

// count peers we've requested the list from
int RequestedMasterNodeList = 0;

//TODO: Rename/move to core
void ThreadCheckMNenginePool()
{
    if(fLiteMode)
	{
		return; //disable all MNengine/Masternode related functionality
	}
	
    // Make this thread recognisable as the wallet flushing thread
    RenameThread("DigitalNote-mnengine");

    unsigned int c = 0;

    while (true)
    {
        MilliSleep(1000);
        
		//LogPrintf("ThreadCheckMNenginePool::check timeout\n");

        // try to sync from all available nodes, one step at a time
        //masternodeSync.Process();
		
        if(mnEnginePool.IsBlockchainSynced())
		{
            c++;

            // check if we should activate or ping every few minutes,
            // start right after sync is considered to be done
            if(c % MASTERNODE_PING_SECONDS == 1)
			{
				activeMasternode.ManageStatus();
			}
			
            if(c % 60 == 0)
            {
                mnodeman.CheckAndRemove();
                mnodeman.ProcessMasternodeConnections();
                masternodePayments.CleanPaymentList();
                CleanTransactionLocksList();
            }

            //if(c % MASTERNODES_DUMP_SECONDS == 0)
			//{
			//	DumpMasternodes();
			//}
			
            mnEnginePool.CheckTimeout();
            mnEnginePool.CheckForCompleteQueue();

            //if(mnEnginePool.GetState() == POOL_STATUS_IDLE && c % 15 == 0)
			//{
				
            //}
        }
    }
}

