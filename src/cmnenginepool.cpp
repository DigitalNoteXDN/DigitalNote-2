#include <random>
#include <openssl/rand.h>
#include <boost/lexical_cast.hpp>

#include "init.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "util.h"
#include "net.h"
#include "cwallet.h"
#include "thread.h"
#include "cblockindex.h"
#include "cwallettx.h"
#include "ckey.h"
#include "cinv.h"
#include "mining.h"
#include "main_extern.h"
#include "cvalidationstate.h"
#include "creservekey.h"
#include "cmasternode.h"
#include "cactivemasternode.h"
#include "ctxdsin.h"
#include "ctxdsout.h"
#include "cmnengineentry.h"
#include "cmnenginequeue.h"
#include "cmnenginebroadcasttx.h"
#include "cmnenginesigner.h"
#include "mnengine.h"
#include "mnengine_extern.h"
#include "script.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "ui_translate.h"

#include "cmnenginepool.h"

// Keep track of the used Masternodes
std::vector<CTxIn> vecMasternodesUsed;

CMNenginePool::CMNenginePool()
{
	/* MNengine uses collateral addresses to trust parties entering the pool
		to behave themselves. If they don't it takes their money. */

	cachedLastSuccess = 0;
	cachedNumBlocks = std::numeric_limits<int>::max();
	unitTest = false;
	txCollateral = CTransaction();
	minBlockSpacing = 0;
	lastNewBlock = 0;

	SetNull();
}

void CMNenginePool::InitCollateralAddress()
{
	SetCollateralAddress(Params().MNenginePoolDummyAddress());
}

void CMNenginePool::SetMinBlockSpacing(int minBlockSpacingIn)
{
	minBlockSpacing = minBlockSpacingIn;
}

void CMNenginePool::Reset()
{
	cachedLastSuccess = 0;
	lastNewBlock = 0;
	txCollateral = CTransaction();
	vecMasternodesUsed.clear();
	UnlockCoins();
	SetNull();
}

void CMNenginePool::SetNull()
{
	// MN side
	sessionUsers = 0;
	vecSessionCollateral.clear();

	// Client side
	entriesCount = 0;
	lastEntryAccepted = 0;
	countEntriesAccepted = 0;
	sessionFoundMasternode = false;

	// Both sides
	state = POOL_STATUS_IDLE;
	sessionID = 0;
	entries.clear();
	finalTransaction.vin.clear();
	finalTransaction.vout.clear();
	lastTimeChanged = GetTimeMillis();

	// -- seed random number generator (used for ordering output lists)
	unsigned int seed = 0;
	RAND_bytes((unsigned char*)&seed, sizeof(seed));
	std::srand(seed);
}

bool CMNenginePool::SetCollateralAddress(const std::string &strAddress)
{
	CDigitalNoteAddress address;

	if (!address.SetString(strAddress))
	{
		LogPrintf("CMNenginePool::SetCollateralAddress - Invalid MNengine collateral address\n");
		
		return false;
	}

	collateralPubKey = GetScriptForDestination(address.Get());

	return true;
}

//
// Unlock coins after MNengine fails or succeeds
//
void CMNenginePool::UnlockCoins()
{
	while(true)
	{
		TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
		
		if(!lockWallet)
		{
			MilliSleep(50);
			
			continue;
		}
		
		for(CTxIn v : lockedCoins)
		{
			pwalletMain->UnlockCoin(v.prevout);
		}

		break;
	}

	lockedCoins.clear();
}

bool CMNenginePool::IsNull() const
{
	return state == POOL_STATUS_ACCEPTING_ENTRIES && entries.empty();
}

int CMNenginePool::GetState() const
{
	return state;
}

int CMNenginePool::GetEntriesCount() const
{
	return entries.size();
}

/// Get the time the last entry was accepted (time in UTC milliseconds)
int CMNenginePool::GetLastEntryAccepted() const
{
	return lastEntryAccepted;
}

/// Get the count of the accepted entries
int CMNenginePool::GetCountEntriesAccepted() const
{
	return countEntriesAccepted;
}

// Set the 'state' value, with some logging and capturing when the state changed
void CMNenginePool::UpdateState(unsigned int newState)
{
	if (fMasterNode && (newState == POOL_STATUS_ERROR || newState == POOL_STATUS_SUCCESS))
	{
		LogPrint("mnengine", "CMNenginePool::UpdateState() - Can't set state to ERROR or SUCCESS as a Masternode. \n");
		
		return;
	}

	LogPrintf("CMNenginePool::UpdateState() == %d | %d \n", state, newState);

	if(state != newState)
	{
		lastTimeChanged = GetTimeMillis();
		
		if(fMasterNode)
		{
			RelayStatus(mnEnginePool.sessionID, mnEnginePool.GetState(), mnEnginePool.GetEntriesCount(), MASTERNODE_RESET);
		}
	}

	state = newState;
}

/// Get the maximum number of transactions for the pool
int CMNenginePool::GetMaxPoolTransactions()
{
	return Params().PoolMaxTransactions();
}

/// Do we have enough users to take entries?
bool CMNenginePool::IsSessionReady()
{
	return sessionUsers >= GetMaxPoolTransactions();
}

bool CMNenginePool::IsBlockchainSynced()
{
	static bool fBlockchainSynced = false;
	static int64_t lastProcess = GetTime();

	// if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
	if(GetTime() - lastProcess > 60*60)
	{
		Reset();
		
		fBlockchainSynced = false;
	}

	lastProcess = GetTime();

	if(fBlockchainSynced) return true;

	if (fImporting || fReindex)
	{
		return false;
	}

	TRY_LOCK(cs_main, lockMain);

	if(!lockMain)
	{
		return false;
	}

	CBlockIndex* pindex = pindexBest;
	if(pindex == NULL)
	{
		return false;
	}

	if(pindex->nTime + 60*60 < GetTime())
	{
		return false;
	}

	fBlockchainSynced = true;

	return true;
}

//
// Check the MNengine progress and send client updates if a Masternode
//
void CMNenginePool::Check()
{
	if(fMasterNode)
	{
		LogPrint("mnengine", "CMNenginePool::Check() - entries count %lu\n", entries.size());
	}

	//printf("CMNenginePool::Check() %d - %d - %d\n", state, anonTx.CountEntries(), GetTimeMillis()-lastTimeChanged);

	if(fMasterNode)
	{
		LogPrint("mnengine", "CMNenginePool::Check() - entries count %lu\n", entries.size());
		
		// If entries is full, then move on to the next phase
		if(state == POOL_STATUS_ACCEPTING_ENTRIES && (int)entries.size() >= GetMaxPoolTransactions())
		{
			LogPrint("mnengine", "CMNenginePool::Check() -- TRYING TRANSACTION \n");
			
			UpdateState(POOL_STATUS_FINALIZE_TRANSACTION);
		}
	}

	// create the finalized transaction for distribution to the clients
	if(state == POOL_STATUS_FINALIZE_TRANSACTION)
	{
		LogPrint("mnengine", "CMNenginePool::Check() -- FINALIZE TRANSACTIONS\n");
		
		UpdateState(POOL_STATUS_SIGNING);

		if (fMasterNode)
		{
			CTransaction txNew;

			// make our new transaction
			for(unsigned int i = 0; i < entries.size(); i++)
			{
				for(const CTxOut& v : entries[i].vout)
				{
					txNew.vout.push_back(v);
				}
			
				for(const CTxDSIn& s : entries[i].sev)
				{
					txNew.vin.push_back(s);
				}
			}

			// shuffle the outputs for improved anonymity
			std::shuffle ( txNew.vin.begin(),  txNew.vin.end(),  std::mt19937(std::random_device()()));
			std::shuffle ( txNew.vout.begin(), txNew.vout.end(), std::mt19937(std::random_device()()));
			
			LogPrint("mnengine", "Transaction 1: %s\n", txNew.ToString());
			
			finalTransaction = txNew;

			// request signatures from clients
			RelayFinalTransaction(sessionID, finalTransaction);
		}
	}

	// If we have all of the signatures, try to compile the transaction
	if(fMasterNode && state == POOL_STATUS_SIGNING && SignaturesComplete())
	{
		LogPrint("mnengine", "CMNenginePool::Check() -- SIGNING\n");
		
		UpdateState(POOL_STATUS_TRANSMISSION);
		CheckFinalTransaction();
	}

	// reset if we're here for 10 seconds
	if((state == POOL_STATUS_ERROR || state == POOL_STATUS_SUCCESS) && GetTimeMillis()-lastTimeChanged >= 10000)
	{
		LogPrint("mnengine", "CMNenginePool::Check() -- timeout, RESETTING\n");
		
		UnlockCoins();
		SetNull();
		
		if(fMasterNode)
		{
			RelayStatus(sessionID, GetState(), GetEntriesCount(), MASTERNODE_RESET);
		}
	}
}

void CMNenginePool::CheckFinalTransaction()
{
	if (!fMasterNode)
	{
		return; // check and relay final tx only on masternode
	}

	CWalletTx txNew = CWalletTx(pwalletMain, finalTransaction);

	LOCK2(cs_main, pwalletMain->cs_wallet);

	{
		LogPrint("mnengine", "Transaction 2: %s\n", txNew.ToString());

		// See if the transaction is valid
		if (!txNew.AcceptToMemoryPool(false, true, true))
		{
			LogPrintf("CMNenginePool::Check() - CommitTransaction : Error: Transaction not valid\n");
			
			SetNull();

			// not much we can do in this case
			UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);
			RelayCompletedTransaction(sessionID, true, ui_translate("Transaction not valid, please try again"));
			
			return;
		}

		LogPrintf("CMNenginePool::Check() -- IS MASTER -- TRANSMITTING MNengine\n");

		// sign a message

		int64_t sigTime = GetAdjustedTime();
		std::string strMessage = txNew.GetHash().ToString() + boost::lexical_cast<std::string>(sigTime);
		std::string strError = "";
		std::vector<unsigned char> vchSig;
		CKey key2;
		CPubKey pubkey2;

		if(!mnEngineSigner.SetKey(strMasterNodePrivKey, strError, key2, pubkey2))
		{
			LogPrintf("CMNenginePool::Check() - ERROR: Invalid Masternodeprivkey: '%s'\n", strError);
			LogPrintf("CMNenginePool::Check() - FORCE BYPASS - SetKey checks!!!\n");
			
			return;
		}

		if(!mnEngineSigner.SignMessage(strMessage, strError, vchSig, key2))
		{
			LogPrintf("CMNenginePool::Check() - Sign message failed\n");
			LogPrintf("CMNenginePool::Check() - FORCE BYPASS - Sign message checks!!!\n");
			
			return;
		}

		if(!mnEngineSigner.VerifyMessage(pubkey2, vchSig, strMessage, strError))
		{
			LogPrintf("CMNenginePool::Check() - Verify message failed\n");
			LogPrintf("CMNenginePool::Check() - FORCE BYPASS - Verify message checks!!!\n");
			
			return;
		}

		std::string txHash = txNew.GetHash().ToString().c_str();
		
		LogPrintf("CMNenginePool::Check() -- txHash %d \n", txHash);
		
		if(!mapMNengineBroadcastTxes.count(txNew.GetHash()))
		{
			CMNengineBroadcastTx dstx;
			dstx.tx = txNew;
			dstx.vin = activeMasternode.vin;
			dstx.vchSig = vchSig;
			dstx.sigTime = sigTime;

			mapMNengineBroadcastTxes.insert(std::make_pair(txNew.GetHash(), dstx));
		}

		CInv inv(MSG_DSTX, txNew.GetHash());
		RelayInventory(inv);

		// Tell the clients it was successful
		RelayCompletedTransaction(sessionID, false, ui_translate("Transaction created successfully."));

		// Randomly charge clients
		ChargeRandomFees();

		// Reset
		LogPrint("mnengine", "CMNenginePool::Check() -- COMPLETED -- RESETTING \n");
		
		SetNull();
		RelayStatus(sessionID, GetState(), GetEntriesCount(), MASTERNODE_RESET);
	}
}

//
// Charge clients a fee if they're abusive
//
// Why bother? MNengine uses collateral to ensure abuse to the process is kept to a minimum.
// The submission and signing stages in mnengine are completely separate. In the cases where
// a client submits a transaction then refused to sign, there must be a cost. Otherwise they
// would be able to do this over and over again and bring the mixing to a hault.
//
// How does this work? Messages to Masternodes come in via "dsi", these require a valid collateral
// transaction for the client to be able to enter the pool. This transaction is kept by the Masternode
// until the transaction is either complete or fails.
//
void CMNenginePool::ChargeFees()
{
	if(!fMasterNode)
	{
		return;
	}

	//we don't need to charge collateral for every offence.
	int offences = 0;
	int r = rand()%100;

	if(r > 33)
	{
		return;
	}

	if(state == POOL_STATUS_ACCEPTING_ENTRIES)
	{
		for(const CTransaction& txCollateral : vecSessionCollateral)
		{
			bool found = false;
			
			for(const CMNengineEntry& v : entries)
			{
				if(v.collateral == txCollateral)
				{
					found = true;
				}
			}

			// This queue entry didn't send us the promised transaction
			if(!found)
			{
				LogPrintf("CMNenginePool::ChargeFees -- found uncooperative node (didn't send transaction). Found offence.\n");
				
				offences++;
			}
		}
	}

	if(state == POOL_STATUS_SIGNING)
	{
		// who didn't sign?
		for(const CMNengineEntry v : entries)
		{
			for(const CTxDSIn s : v.sev)
			{
				if(!s.fHasSig)
				{
					LogPrintf("CMNenginePool::ChargeFees -- found uncooperative node (didn't sign). Found offence\n");
					
					offences++;
				}
			}
		}
	}

	r = rand()%100;
	int target = 0;

	//mostly offending?
	if(offences >= Params().PoolMaxTransactions()-1 && r > 33)
	{
		return;
	}

	//everyone is an offender? That's not right
	if(offences >= Params().PoolMaxTransactions())
	{
		return;
	}

	//charge one of the offenders randomly
	if(offences > 1)
	{
		target = 50;
	}

	//pick random client to charge
	r = rand()%100;

	if(state == POOL_STATUS_ACCEPTING_ENTRIES)
	{
		for(const CTransaction& txCollateral : vecSessionCollateral)
		{
			bool found = false;
			
			for(const CMNengineEntry& v : entries)
			{
				if(v.collateral == txCollateral)
				{
					found = true;
				}
			}

			// This queue entry didn't send us the promised transaction
			if(!found && r > target)
			{
				LogPrintf("CMNenginePool::ChargeFees -- found uncooperative node (didn't send transaction). charging fees.\n");

				CWalletTx wtxCollateral = CWalletTx(pwalletMain, txCollateral);

				// Broadcast
				if (!wtxCollateral.AcceptToMemoryPool(true))
				{
					// This must not fail. The transaction has already been signed and recorded.
					LogPrintf("CMNenginePool::ChargeFees() : Error: Transaction not valid");
				}
				
				wtxCollateral.RelayWalletTransaction();
				
				return;
			}
		}
	}

	if(state == POOL_STATUS_SIGNING)
	{
		// who didn't sign?
		for(const CMNengineEntry v : entries)
		{
			for(const CTxDSIn s : v.sev)
			{
				if(!s.fHasSig && r > target)
				{
					LogPrintf("CMNenginePool::ChargeFees -- found uncooperative node (didn't sign). charging fees.\n");

					CWalletTx wtxCollateral = CWalletTx(pwalletMain, v.collateral);

					// Broadcast
					if (!wtxCollateral.AcceptToMemoryPool(false))
					{
						// This must not fail. The transaction has already been signed and recorded.
						LogPrintf("CMNenginePool::ChargeFees() : Error: Transaction not valid");
					}
					
					wtxCollateral.RelayWalletTransaction();
					
					return;
				}
			}
		}
	}
}

// charge the collateral randomly
//  - MNengine is completely free, to pay miners we randomly pay the collateral of users.
void CMNenginePool::ChargeRandomFees()
{
	if(fMasterNode)
	{
		int i = 0;

		for(const CTransaction& txCollateral : vecSessionCollateral)
		{
			int r = rand()%100;

			/*
				Collateral Fee Charges:

				Being that MNengine has "no fees" we need to have some kind of cost associated
				with using it to stop abuse. Otherwise it could serve as an attack vector and
				allow endless transaction that would bloat DigitalNote and make it unusable. To
				stop these kinds of attacks 1 in 50 successful transactions are charged. This
				adds up to a cost of 0.002 XDN per transaction on average.
			*/
			if(r <= 10)
			{
				LogPrintf("CMNenginePool::ChargeRandomFees -- charging random fees. %u\n", i);

				CWalletTx wtxCollateral = CWalletTx(pwalletMain, txCollateral);

				// Broadcast
				if (!wtxCollateral.AcceptToMemoryPool(true))
				{
					// This must not fail. The transaction has already been signed and recorded.
					LogPrintf("CMNenginePool::ChargeRandomFees() : Error: Transaction not valid");
				}
				
				wtxCollateral.RelayWalletTransaction();
			}
		}
	}
}

//
// Check for various timeouts (queue objects, mnengine, etc)
//
void CMNenginePool::CheckTimeout()
{
	// catching hanging sessions
	if(!fMasterNode)
	{
		switch(state)
		{
			case POOL_STATUS_TRANSMISSION:
				LogPrint("mnengine", "CMNenginePool::CheckTimeout() -- Session complete -- Running Check()\n");
				
				Check();
			break;
			case POOL_STATUS_ERROR:
				LogPrint("mnengine", "CMNenginePool::CheckTimeout() -- Pool error -- Running Check()\n");
				
				Check();
			break;
			case POOL_STATUS_SUCCESS:
				LogPrint("mnengine", "CMNenginePool::CheckTimeout() -- Pool success -- Running Check()\n");
				
				Check();
				
			break;
		}
	}

	// check MNengine queue objects for timeouts
	int c = 0;
	std::vector<CMNengineQueue>::iterator it = vecMNengineQueue.begin();

	while(it != vecMNengineQueue.end())
	{
		if((*it).IsExpired())
		{
			LogPrint("mnengine", "CMNenginePool::CheckTimeout() : Removing expired queue entry - %d\n", c);
			
			it = vecMNengineQueue.erase(it);
		}
		else
		{
			++it;
		}
		
		c++;
	}

	int addLagTime = 0;
	if(!fMasterNode)
	{
		addLagTime = 10000; //if we're the client, give the server a few extra seconds before resetting.
	}

	if(state == POOL_STATUS_ACCEPTING_ENTRIES || state == POOL_STATUS_QUEUE)
	{
		c = 0;

		// check for a timeout and reset if needed
		std::vector<CMNengineEntry>::iterator it2 = entries.begin();
		
		while(it2 != entries.end())
		{
			if((*it2).IsExpired())
			{
				LogPrint("mnengine", "CMNenginePool::CheckTimeout() : Removing expired entry - %d\n", c);
				
				it2 = entries.erase(it2);
				
				if(entries.size() == 0)
				{
					UnlockCoins();
					SetNull();
				}
				
				if(fMasterNode)
				{
					RelayStatus(sessionID, GetState(), GetEntriesCount(), MASTERNODE_RESET);
				}
			}
			else
			{
				++it2;
			}
			
			c++;
		}

		if(GetTimeMillis()-lastTimeChanged >= (MNengine_QUEUE_TIMEOUT*1000)+addLagTime)
		{
			UnlockCoins();
			SetNull();
		}
	}
	else if(GetTimeMillis()-lastTimeChanged >= (MNengine_QUEUE_TIMEOUT*1000)+addLagTime)
	{
		LogPrint("mnengine", "CMNenginePool::CheckTimeout() -- Session timed out (%ds) -- resetting\n", MNengine_QUEUE_TIMEOUT);
		
		UnlockCoins();
		SetNull();

		UpdateState(POOL_STATUS_ERROR);
		lastMessage = ui_translate("Session timed out.");
	}

	if(state == POOL_STATUS_SIGNING && GetTimeMillis()-lastTimeChanged >= (MNengine_SIGNING_TIMEOUT*1000)+addLagTime )
	{
		LogPrint("mnengine", "CMNenginePool::CheckTimeout() -- Session timed out (%ds) -- restting\n", MNengine_SIGNING_TIMEOUT);
		
		ChargeFees();
		UnlockCoins();
		SetNull();

		UpdateState(POOL_STATUS_ERROR);
		lastMessage = ui_translate("Signing timed out.");
	}
}

//
// Check for complete queue
//
void CMNenginePool::CheckForCompleteQueue()
{
	if(!fMasterNode)
	{
		return;
	}

	/* Check to see if we're ready for submissions from clients */
	//
	// After receiving multiple dsa messages, the queue will switch to "accepting entries"
	// which is the active state right before merging the transaction
	//
	if(state == POOL_STATUS_QUEUE && sessionUsers == GetMaxPoolTransactions())
	{
		UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);

		CMNengineQueue dsq;
		dsq.vin = activeMasternode.vin;
		dsq.time = GetTime();
		dsq.ready = true;
		dsq.Sign();
		dsq.Relay();
	}
}

// check to see if the signature is valid
bool CMNenginePool::SignatureValid(const CScript& newSig, const CTxIn& newVin)
{
    CTransaction txNew;
    txNew.vin.clear();
    txNew.vout.clear();

    int found = -1;
    CScript sigPubKey = CScript();
    unsigned int i = 0;

    for(CMNengineEntry& e : entries)
	{
        for(const CTxOut& out : e.vout)
		{
            txNew.vout.push_back(out);
		}
		
        for(const CTxDSIn& s : e.sev)
		{
            txNew.vin.push_back(s);

            if(s == newVin)
			{
                found = i;
                sigPubKey = s.prevPubKey;
            }
			
            i++;
        }
    }

    if(found >= 0) //might have to do this one input at a time?
	{
        int n = found;
        txNew.vin[n].scriptSig = newSig;
        
		LogPrint("mnengine", "CMNenginePool::SignatureValid() - Sign with sig %s\n", newSig.ToString().substr(0,24));
        
		if (!VerifyScript(txNew.vin[n].scriptSig, sigPubKey, txNew, n, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, 0))
		{
            LogPrint("mnengine", "CMNenginePool::SignatureValid() - Signing - Error signing input %u\n", n);
            
			return false;
        }
    }

    LogPrint("mnengine", "CMNenginePool::SignatureValid() - Signing - Successfully validated input\n");
    
	return true;
}

// check to make sure the collateral provided by the client is valid
bool CMNenginePool::IsCollateralValid(const CTransaction& txCollateral)
{
	if(txCollateral.vout.size() < 1)
	{
		return false;
	}

	if(txCollateral.nLockTime != 0)
	{
		return false;
	}

	int64_t nValueIn = 0;
	int64_t nValueOut = 0;
	bool missingTx = false;

	for(const CTxOut o : txCollateral.vout)
	{
		nValueOut += o.nValue;

		if(!o.scriptPubKey.IsNormalPaymentScript())
		{
			LogPrintf("CMNenginePool::IsCollateralValid - Invalid Script %s\n", txCollateral.ToString());
			
			return false;
		}
	}

	for(const CTxIn i : txCollateral.vin)
	{
		CTransaction tx2;
		uint256 hash;
		
		if(GetTransaction(i.prevout.hash, tx2, hash))
		{
			if(tx2.vout.size() > i.prevout.n)
			{
				nValueIn += tx2.vout[i.prevout.n].nValue;
			}
		}
		else
		{
			missingTx = true;
		}
	}

	if(missingTx)
	{
		LogPrint("mnengine", "CMNenginePool::IsCollateralValid - Unknown inputs in collateral transaction - %s\n", txCollateral.ToString());
		
		return false;
	}

	//collateral transactions are required to pay out MNengine_COLLATERAL as a fee to the miners
	if(nValueIn-nValueOut < MNengine_COLLATERAL)
	{
		LogPrint("mnengine", "CMNenginePool::IsCollateralValid - did not include enough fees in transaction %d\n%s\n", nValueOut-nValueIn, txCollateral.ToString());
		
		return false;
	}

	LogPrint("mnengine", "CMNenginePool::IsCollateralValid %s\n", txCollateral.ToString());

	{
		LOCK(cs_main);
		
		CValidationState state;
		
		if(!AcceptableInputs(mempool, txCollateral, true, NULL))
		{
			LogPrintf ("CMNenginePool::IsCollateralValid - didn't pass IsAcceptable\n");
			
			return false;
		}
	}

	return true;
}

//
// Add a clients transaction to the pool
//
bool CMNenginePool::AddEntry(const std::vector<CTxIn>& newInput, const int64_t& nAmount, const CTransaction& txCollateral, 
		const std::vector<CTxOut>& newOutput, std::string& error)
{
	if (!fMasterNode)
	{
		return false;
	}

	for(CTxIn in : newInput)
	{
		if (in.prevout.IsNull() || nAmount < 0)
		{
			LogPrint("mnengine", "CMNenginePool::AddEntry - input not valid!\n");
			error = ui_translate("Input is not valid.");
			
			sessionUsers--;
			
			return false;
		}
	}

	if (!IsCollateralValid(txCollateral))
	{
		LogPrint("mnengine", "CMNenginePool::AddEntry - collateral not valid!\n");
		error = ui_translate("Collateral is not valid.");
		
		sessionUsers--;
		
		return false;
	}

	if((int)entries.size() >= GetMaxPoolTransactions())
	{
		LogPrint("mnengine", "CMNenginePool::AddEntry - entries is full!\n");
		
		error = ui_translate("Entries are full.");
		
		sessionUsers--;
		
		return false;
	}

	for(CTxIn in : newInput)
	{
		LogPrint("mnengine", "looking for vin -- %s\n", in.ToString());
		
		for(const CMNengineEntry& v : entries)
		{
			for(const CTxDSIn& s : v.sev)
			{
				if((CTxIn)s == in)
				{
					LogPrint("mnengine", "CMNenginePool::AddEntry - found in vin\n");
					error = ui_translate("Already have that input.");
					
					sessionUsers--;
					
					return false;
				}
			}
		}
	}

	CMNengineEntry v;
	v.Add(newInput, nAmount, txCollateral, newOutput);
	entries.push_back(v);

	LogPrint("mnengine", "CMNenginePool::AddEntry -- adding %s\n", newInput[0].ToString());
	error = "";

	return true;
}

bool CMNenginePool::AddScriptSig(const CTxIn& newVin)
{
	LogPrint("mnengine", "CMNenginePool::AddScriptSig -- new sig  %s\n", newVin.scriptSig.ToString().substr(0,24));

	for(const CMNengineEntry& v : entries)
	{
		for(const CTxDSIn& s : v.sev)
		{
			if(s.scriptSig == newVin.scriptSig)
			{
				LogPrint("mnengine", "CMNenginePool::AddScriptSig - already exists \n");
				
				return false;
			}
		}
	}

	if(!SignatureValid(newVin.scriptSig, newVin))
	{
		LogPrint("mnengine", "CMNenginePool::AddScriptSig - Invalid Sig\n");
		
		return false;
	}

	LogPrint("mnengine", "CMNenginePool::AddScriptSig -- sig %s\n", newVin.ToString());

	for(CTxIn& vin : finalTransaction.vin)
	{
		if(newVin.prevout == vin.prevout && vin.nSequence == newVin.nSequence)
		{
			vin.scriptSig = newVin.scriptSig;
			vin.prevPubKey = newVin.prevPubKey;
			
			LogPrint("mnengine", "CMNenginePool::AddScriptSig -- adding to finalTransaction  %s\n", newVin.scriptSig.ToString().substr(0,24));
		}
	}

	for(unsigned int i = 0; i < entries.size(); i++)
	{
		if(entries[i].AddSig(newVin))
		{
			LogPrint("mnengine", "CMNenginePool::AddScriptSig -- adding  %s\n", newVin.scriptSig.ToString().substr(0,24));
			
			return true;
		}
	}

	LogPrintf("CMNenginePool::AddScriptSig -- Couldn't set sig!\n" );

	return false;
}

// check to make sure everything is signed
bool CMNenginePool::SignaturesComplete()
{
	for(const CMNengineEntry& v : entries)
	{
		for(const CTxDSIn& s : v.sev)
		{
			if(!s.fHasSig)
			{
				return false;
			}
		}
	}

	return true;
}

// Incoming message from Masternode updating the progress of mnengine
//    newAccepted:  -1 mean's it'n not a "transaction accepted/not accepted" message, just a standard update
//                  0 means transaction was not accepted
//                  1 means transaction was accepted

bool CMNenginePool::StatusUpdate(int newState, int newEntriesCount, int newAccepted, std::string& error, int newSessionID)
{
	if(fMasterNode)
	{
		return false;
	}

	if(state == POOL_STATUS_ERROR || state == POOL_STATUS_SUCCESS)
	{
		return false;
	}

	UpdateState(newState);
	entriesCount = newEntriesCount;

	if(newAccepted != -1)
	{
		lastEntryAccepted = newAccepted;
		countEntriesAccepted += newAccepted;
		
		if(newAccepted == 0)
		{
			UpdateState(POOL_STATUS_ERROR);
			lastMessage = error;
		}

		if(newAccepted == 1 && newSessionID != 0)
		{
			sessionID = newSessionID;
			
			LogPrintf("CMNenginePool::StatusUpdate - set sessionID to %d\n", sessionID);
			
			sessionFoundMasternode = true;
		}
	}

	if(newState == POOL_STATUS_ACCEPTING_ENTRIES)
	{
		if(newAccepted == 1)
		{
			LogPrintf("CMNenginePool::StatusUpdate - entry accepted! \n");
			
			sessionFoundMasternode = true;
			//wait for other users. Masternode will report when ready
			UpdateState(POOL_STATUS_QUEUE);
		}
		else if (newAccepted == 0 && sessionID == 0 && !sessionFoundMasternode)
		{
			LogPrintf("CMNenginePool::StatusUpdate - entry not accepted by Masternode \n");
			
			UnlockCoins();
			UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);
		}
		
		if(sessionFoundMasternode)
		{
			return true;
		}
	}

	return true;
}

//
// After we receive the finalized transaction from the Masternode, we must
// check it to make sure it's what we want, then sign it if we agree.
// If we refuse to sign, it's possible we'll be charged collateral
//
bool CMNenginePool::SignFinalTransaction(CTransaction& finalTransactionNew, CNode* node)
{
	if(fMasterNode)
	{
		return false;
	}

	finalTransaction = finalTransactionNew;
	LogPrintf("CMNenginePool::SignFinalTransaction %s\n", finalTransaction.ToString());

	std::vector<CTxIn> sigs;

	//make sure my inputs/outputs are present, otherwise refuse to sign
	for(const CMNengineEntry e : entries)
	{
		for(const CTxDSIn s : e.sev)
		{
			/* Sign my transaction and all outputs */
			int mine = -1;
			CScript prevPubKey = CScript();
			CTxIn vin = CTxIn();

			for(unsigned int i = 0; i < finalTransaction.vin.size(); i++)
			{
				if(finalTransaction.vin[i] == s)
				{
					mine = i;
					prevPubKey = s.prevPubKey;
					vin = s;
				}
			}
			
			if(mine >= 0) //might have to do this one input at a time?
			{
				int foundOutputs = 0;
				CAmount nValue1 = 0;
				CAmount nValue2 = 0;

				for(unsigned int i = 0; i < finalTransaction.vout.size(); i++)
				{
					for(const CTxOut& o : e.vout)
					{
						std::string Ftx = finalTransaction.vout[i].scriptPubKey.ToString().c_str();
						std::string Otx = o.scriptPubKey.ToString().c_str();
						
						if(Ftx == Otx)
						{
							//if(fDebug) LogPrintf("CMNenginePool::SignFinalTransaction - foundOutputs = %d \n", foundOutputs);
							foundOutputs++;
							nValue1 += finalTransaction.vout[i].nValue;
						}
					}
				}

				for(const CTxOut o : e.vout)
				{
					nValue2 += o.nValue;
				}
				
				int targetOuputs = e.vout.size();
				if(foundOutputs < targetOuputs || nValue1 != nValue2)
				{
					// in this case, something went wrong and we'll refuse to sign. It's possible we'll be charged collateral. But that's
					// better then signing if the transaction doesn't look like what we wanted.
					LogPrintf("CMNenginePool::Sign - My entries are not correct! Refusing to sign. %d entries %d target. \n", foundOutputs, targetOuputs);
					
					UnlockCoins();
					SetNull();
					
					return false;
				}

				const CKeyStore& keystore = *pwalletMain;

				LogPrint("mnengine", "CMNenginePool::Sign - Signing my input %i\n", mine);
				
				if(!SignSignature(keystore, prevPubKey, finalTransaction, mine, int(SIGHASH_ALL|SIGHASH_ANYONECANPAY))) // changes scriptSig
				{
					LogPrint("mnengine", "CMNenginePool::Sign - Unable to sign my own transaction! \n");
					// not sure what to do here, it will timeout...?
				}

				sigs.push_back(finalTransaction.vin[mine]);
				
				LogPrint("mnengine", " -- dss %d %d %s\n", mine, (int)sigs.size(), finalTransaction.vin[mine].scriptSig.ToString());
			}
		}
		
		LogPrint("mnengine", "CMNenginePool::Sign - txNew:\n%s", finalTransaction.ToString());
	}

	// push all of our signatures to the Masternode
	if(sigs.size() > 0 && node != NULL)
	{
		node->PushMessage("dss", sigs);
	}

	return true;
}

void CMNenginePool::NewBlock()
{
	LogPrint("mnengine", "CMNenginePool::NewBlock \n");

	//we we're processing lots of blocks, we'll just leave
	if(GetTime() - lastNewBlock < 10)
	{
		return;
	}

	lastNewBlock = GetTime();

	mnEnginePool.CheckTimeout();
}

// MNengine transaction was completed (failed or successful)
void CMNenginePool::CompletedTransaction(bool error, int errorID)
{
	if(fMasterNode)
	{
		return;
	}

	if(error)
	{
		LogPrintf("CompletedTransaction -- error \n");
		
		UpdateState(POOL_STATUS_ERROR);
		Check();
		UnlockCoins();
		SetNull();
	}
	else
	{
		LogPrintf("CompletedTransaction -- success \n");
		
		UpdateState(POOL_STATUS_SUCCESS);

		UnlockCoins();
		SetNull();
		
		// To avoid race conditions, we'll only let DS run once per block
		cachedLastSuccess = pindexBest->nHeight;
	}

	lastMessage = GetMessageByID(errorID);
}

void CMNenginePool::ClearLastMessage()
{
	lastMessage = "";
}

bool CMNenginePool::SendRandomPaymentToSelf()
{
	int64_t nBalance = pwalletMain->GetBalance();
	int64_t nPayment = (nBalance*0.35) + (rand() % nBalance);

	if(nPayment > nBalance)
	{
		nPayment = nBalance-(0.1*COIN);
	}

	// make our change address
	CReserveKey reservekey(pwalletMain);

	CScript scriptChange;
	CPubKey vchPubKey;

	assert(reservekey.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked

	scriptChange = GetScriptForDestination(vchPubKey.GetID());

	CWalletTx wtx;
	int64_t nFeeRet = 0;
	std::string strFail = "";
	std::vector<std::pair<CScript, int64_t> > vecSend;

	// ****** Add fees ************ /
	vecSend.push_back(std::make_pair(scriptChange, nPayment));

	CCoinControl *coinControl=NULL;
	int32_t nChangePos;
	bool success = pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRet, nChangePos, strFail, coinControl);
	
	if(!success)
	{
		LogPrintf("SendRandomPaymentToSelf: Error - %s\n", strFail);
		
		return false;
	}

	pwalletMain->CommitTransaction(wtx, reservekey);

	LogPrintf("SendRandomPaymentToSelf Success: tx %s\n", wtx.GetHash().GetHex());

	return true;
}

// Split up large inputs or create fee sized inputs
bool CMNenginePool::MakeCollateralAmounts()
{
    CWalletTx wtx;
    int64_t nFeeRet = 0;
    std::string strFail = "";
    std::vector<std::pair<CScript, int64_t>> vecSend;
    CCoinControl *coinControl = NULL;

    // make our collateral address
    CReserveKey reservekeyCollateral(pwalletMain);
    // make our change address
    CReserveKey reservekeyChange(pwalletMain);

    CScript scriptCollateral;
    CPubKey vchPubKey;
    assert(reservekeyCollateral.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked
    scriptCollateral = GetScriptForDestination(vchPubKey.GetID());

    vecSend.push_back(std::make_pair(scriptCollateral, MNengine_COLLATERAL*4));

    int32_t nChangePos;
    // try to use non-denominated and not mn-like funds
    bool success = pwalletMain->CreateTransaction(
		vecSend,
		wtx,
		reservekeyChange,
        nFeeRet,
		nChangePos,
		strFail,
		coinControl,
		ONLY_NONDENOMINATED_NOT10000IFMN
	);
    
	if(!success)
	{
        // if we failed (most likeky not enough funds), try to use denominated instead -
        // MN-like funds should not be touched in any case and we can't mix denominated without collaterals anyway
        LogPrintf("MakeCollateralAmounts: ONLY_NONDENOMINATED_NOT1000IFMN Error - %s\n", strFail);
        
		success = pwalletMain->CreateTransaction(
			vecSend,
			wtx,
			reservekeyChange,
            nFeeRet,
			nChangePos,
			strFail,
			coinControl,
			ONLY_NOT10000IFMN
		);
        
		if(!success)
		{
            LogPrintf("MakeCollateralAmounts: ONLY_NOT1000IFMN Error - %s\n", strFail);
            
			reservekeyCollateral.ReturnKey();
            
			return false;
        }
    }

    reservekeyCollateral.KeepKey();

    LogPrintf("MakeCollateralAmounts: tx %s\n", wtx.GetHash().GetHex());

    // use the same cachedLastSuccess as for DS mixinx to prevent race
    if(!pwalletMain->CommitTransaction(wtx, reservekeyChange))
	{
        LogPrintf("MakeCollateralAmounts: CommitTransaction failed!\n");
        
		return false;
    }

    cachedLastSuccess = pindexBest->nHeight;

    return true;
}

std::string CMNenginePool::GetMessageByID(int messageID)
{
	switch (messageID)
	{
		case ERR_ALREADY_HAVE: 			return ui_translate("Already have that input.");
		case ERR_ENTRIES_FULL: 			return ui_translate("Entries are full.");
		case ERR_EXISTING_TX: 			return ui_translate("Not compatible with existing transactions.");
		case ERR_FEES: 					return ui_translate("Transaction fees are too high.");
		case ERR_INVALID_COLLATERAL: 	return ui_translate("Collateral not valid.");
		case ERR_INVALID_INPUT: 		return ui_translate("Input is not valid.");
		case ERR_INVALID_SCRIPT: 		return ui_translate("Invalid script detected.");
		case ERR_INVALID_TX: 			return ui_translate("Transaction not valid.");
		case ERR_MAXIMUM: 				return ui_translate("Value more than MNengine pool maximum allows.");
		case ERR_MN_LIST: 				return ui_translate("Not in the Masternode list.");
		case ERR_MODE: 					return ui_translate("Incompatible mode.");
		case ERR_NON_STANDARD_PUBKEY: 	return ui_translate("Non-standard public key detected.");
		case ERR_NOT_A_MN: 				return ui_translate("This is not a Masternode.");
		case ERR_QUEUE_FULL: 			return ui_translate("Masternode queue is full.");
		case ERR_RECENT: 				return ui_translate("Last MNengine was too recent.");
		case ERR_SESSION:				return ui_translate("Session not complete!");
		case ERR_MISSING_TX:			return ui_translate("Missing input transaction information.");
		case ERR_VERSION:				return ui_translate("Incompatible version.");
		case MSG_SUCCESS:				return ui_translate("Transaction created successfully.");
		case MSG_ENTRIES_ADDED: 		return ui_translate("Your entries added successfully.");
		case MSG_NOERR:
		default:						return "";
	}
}

void CMNenginePool::RelayFinalTransaction(const int sessionID, const CTransaction& txNew)
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		pnode->PushMessage("dsf", sessionID, txNew);
	}
}

void CMNenginePool::RelayIn(const std::vector<CTxDSIn>& vin, const int64_t& nAmount, const CTransaction& txCollateral, const std::vector<CTxDSOut>& vout)
{
	if(!pSubmittedToMasternode)
	{
		return;
	}

	std::vector<CTxIn> vin2;
	std::vector<CTxOut> vout2;

	for(CTxDSIn in : vin)
	{
		vin2.push_back(in);
	}

	for(CTxDSOut out : vout)
	{
		vout2.push_back(out);
	}

	CNode* pnode = FindNode(pSubmittedToMasternode->addr);
	if(pnode != NULL)
	{
		LogPrintf("RelayIn - found master, relaying message - %s \n", pnode->addr.ToString());
		
		pnode->PushMessage("dsi", vin2, nAmount, txCollateral, vout2);
	}
}

void CMNenginePool::RelayStatus(const int sessionID, const int newState, const int newEntriesCount, const int newAccepted, const std::string &error)
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		pnode->PushMessage("dssu", sessionID, newState, newEntriesCount, newAccepted, error);
	}
}

void CMNenginePool::RelayCompletedTransaction(const int sessionID, const bool error, const std::string &errorMessage)
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		pnode->PushMessage("dsc", sessionID, error, errorMessage);
	}
}

