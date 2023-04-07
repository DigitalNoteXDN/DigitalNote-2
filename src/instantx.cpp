#include "compat.h"

#include <map>

#include "util.h"
#include "spork.h"
#include "mnengine_extern.h"
#include "cmnenginepool.h"
#include "cdatastream.h"
#include "cinv.h"
#include "net.h"
#include "ctxout.h"
#include "cvalidationstate.h"
#include "cblock.h"
#include "txdb-leveldb.h"
#include "main_extern.h"
#include "thread.h"
#include "main.h"
#include "ctxin.h"
#include "ctransactionlock.h"
#include "blockparams.h"
#include "cconsensusvote.h"
#include "main_const.h"
#include "util/backwards.h"
#include "cblockindex.h"
#include "cactivemasternode.h"
#include "masternode_extern.h"
#include "cmasternodeman.h"
#include "version.h"
#include "cmasternode.h"
#include "init.h"
#include "cwallet.h"

#include "instantx.h"

std::map<uint256, CTransaction> mapTxLockReq;
std::map<uint256, CTransaction> mapTxLockReqRejected;
std::map<uint256, CConsensusVote> mapTxLockVote;
std::map<uint256, CTransactionLock> mapTxLocks;
std::map<COutPoint, uint256> mapLockedInputs;
std::map<uint256, int64_t> mapUnknownVotes; //track votes with no tx for DOS
int nCompleteTXLocks;

//txlock - Locks transaction
//
//step 1.) Broadcast intention to lock transaction inputs, "txlreg", CTransaction
//step 2.) Top 10 masternodes, open connect to top 1 masternode. Send "txvote", CTransaction, Signature, Approve
//step 3.) Top 1 masternode, waits for 10 messages. Upon success, sends "txlock'

void ProcessMessageInstantX(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
	if(fLiteMode)
	{
		return; //disable all mnengine/masternode related functionality
	}

	if(!IsSporkActive(SPORK_2_INSTANTX))
	{
		return;
	}

	if(!mnEnginePool.IsBlockchainSynced())
	{
		return;
	}

	if (strCommand == "txlreq")
	{
		//LogPrintf("ProcessMessageInstantX::txlreq\n");
		CDataStream vMsg(vRecv);
		CTransaction tx;
		vRecv >> tx;

		CInv inv(MSG_TXLOCK_REQUEST, tx.GetHash());
		pfrom->AddInventoryKnown(inv);

		if(mapTxLockReq.count(tx.GetHash()) || mapTxLockReqRejected.count(tx.GetHash()))
		{
			return;
		}

		if(!IsIXTXValid(tx))
		{
			return;
		}

		for(const CTxOut o : tx.vout)
		{
			if(!o.scriptPubKey.IsNormalPaymentScript() && !o.scriptPubKey.IsUnspendable())
			{
				LogPrintf("ProcessMessageInstantX::txlreq - Invalid Script %s\n", tx.ToString().c_str());
				
				return;
			}
		}

		int nBlockHeight = CreateNewLock(tx);

		bool fMissingInputs = false;
		CBlockIndex* pindex;
		CValidationState state;
		CBlock block;
		CTxDB txdb("r");

		bool fAccepted = false;
		
		{
			LOCK(cs_main);
			
			fAccepted = AcceptToMemoryPool(mempool, tx, true, &fMissingInputs);
		}
		
		if (fAccepted)
		{
			RelayInventory(inv);

			DoConsensusVote(tx, nBlockHeight);

			mapTxLockReq.insert(std::make_pair(tx.GetHash(), tx));

			LogPrintf("ProcessMessageInstantX::txlreq - Transaction Lock Request: %s %s : accepted %s\n",
				pfrom->addr.ToString().c_str(),
				pfrom->cleanSubVer.c_str(),
				tx.GetHash().ToString().c_str()
			);

			return;
		}
		else
		{
			mapTxLockReqRejected.insert(std::make_pair(tx.GetHash(), tx));

			// can we get the conflicting transaction as proof?
			LogPrintf("ProcessMessageInstantX::txlreq - Transaction Lock Request: %s %s : rejected %s\n",
				pfrom->addr.ToString().c_str(),
				pfrom->cleanSubVer.c_str(),
				tx.GetHash().ToString().c_str()
			);

			for(const CTxIn& in : tx.vin)
			{
				if(!mapLockedInputs.count(in.prevout))
				{
					mapLockedInputs.insert(std::make_pair(in.prevout, tx.GetHash()));
				}
			}
			
			// resolve conflicts
			std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(tx.GetHash());
			if (i != mapTxLocks.end())
			{
				//we only care if we have a complete tx lock
				if((*i).second.CountSignatures() >= INSTANTX_SIGNATURES_REQUIRED)
				{
					if(!CheckForConflictingLocks(tx))
					{
						LogPrintf("ProcessMessageInstantX::txlreq - Found Existing Complete IX Lock\n");

						//reprocess the last 15 blocks
						block.DisconnectBlock(txdb, pindex);
						tx.DisconnectInputs(txdb);
					}
				}
			}

			return;
		}
	}
	else if (strCommand == "txlvote") //InstantX Lock Consensus Votes
	{
		CConsensusVote ctx;
		vRecv >> ctx;

		CInv inv(MSG_TXLOCK_VOTE, ctx.GetHash());
		pfrom->AddInventoryKnown(inv);

		if(mapTxLockVote.count(ctx.GetHash()))
		{
			return;
		}

		mapTxLockVote.insert(std::make_pair(ctx.GetHash(), ctx));

		if(ProcessConsensusVote(pfrom, ctx))
		{
			//Spam/Dos protection
			/*
				Masternodes will sometimes propagate votes before the transaction is known to the client.
				This tracks those messages and allows it at the same rate of the rest of the network, if
				a peer violates it, it will simply be ignored
			*/
			if(!mapTxLockReq.count(ctx.txHash) && !mapTxLockReqRejected.count(ctx.txHash))
			{
				if(!mapUnknownVotes.count(ctx.vinMasternode.prevout.hash))
				{
					mapUnknownVotes[ctx.vinMasternode.prevout.hash] = GetTime()+(60*10);
				}

				if(mapUnknownVotes[ctx.vinMasternode.prevout.hash] > GetTime() &&
					mapUnknownVotes[ctx.vinMasternode.prevout.hash] - GetAverageVoteTime() > 60*10)
				{
					LogPrintf(
						"ProcessMessageInstantX::txlreq - masternode is spamming transaction votes: %s %s\n",
						ctx.vinMasternode.ToString().c_str(),
						ctx.txHash.ToString().c_str()
					);
					
					return;
				}
				else
				{
					mapUnknownVotes[ctx.vinMasternode.prevout.hash] = GetTime()+(60*10);
				}
			}

			RelayInventory(inv);
		}

		return;
	}
}

bool IsIXTXValid(const CTransaction& txCollateral)
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

	if(nValueOut > GetSporkValue(SPORK_5_MAX_VALUE)*COIN)
	{
		LogPrint("instantx", "IsIXTXValid - Transaction value too high - %s\n", txCollateral.ToString().c_str());
		
		return false;
	}

	if(missingTx)
	{
		LogPrint("instantx", "IsIXTXValid - Unknown inputs in IX transaction - %s\n", txCollateral.ToString().c_str());
		/*
			This happens sometimes for an unknown reason, so we'll return that it's a valid transaction.
			If someone submits an invalid transaction it will be rejected by the network anyway and this isn't
			very common, but we don't want to block IX just because the client can't figure out the fee.
		*/
		return true;
	}

	if(nValueIn-nValueOut < COIN*0.01)
	{
		LogPrint(
			"instantx", "IsIXTXValid - did not include enough fees in transaction %d\n%s\n",
			nValueOut-nValueIn,
			txCollateral.ToString().c_str()
		);
		
		return false;
	}

	return true;
}

int64_t CreateNewLock(CTransaction tx)
{
	int64_t nTxAge = 0;

	for(CTxIn i : backwards<std::vector<CTxIn>>(tx.vin))
	{
		nTxAge = GetInputAge(i);
		
		if(nTxAge < 9)
		{
			LogPrintf("CreateNewLock - Transaction not found / too new: %d / %s\n", nTxAge, tx.GetHash().ToString().c_str());
			
			return 0;
		}
	}

	/*
		Use a blockheight newer than the input.
		This prevents attackers from using transaction mallibility to predict which masternodes
		they'll use.
	*/
	int nBlockHeight = (pindexBest->nHeight - nTxAge)+4;

	if (!mapTxLocks.count(tx.GetHash()))
	{
		LogPrintf("CreateNewLock - New Transaction Lock %s !\n", tx.GetHash().ToString().c_str());

		CTransactionLock newLock;
		
		newLock.nBlockHeight = nBlockHeight;
		newLock.nExpiration = GetTime()+(20*60); //locks expire after 20 minutes (20 confirmations)
		newLock.nTimeout = GetTime()+(60*5);
		newLock.txHash = tx.GetHash();
		
		mapTxLocks.insert(std::make_pair(tx.GetHash(), newLock));
	}
	else
	{
		mapTxLocks[tx.GetHash()].nBlockHeight = nBlockHeight;
		
		LogPrint("instantx", "CreateNewLock - Transaction Lock Exists %s !\n", tx.GetHash().ToString().c_str());
	}

	return nBlockHeight;
}

// check if we need to vote on this transaction
void DoConsensusVote(CTransaction& tx, int64_t nBlockHeight)
{
	if(!fMasterNode)
	{
		return;
	}
	
	int n = mnodeman.GetMasternodeRank(activeMasternode.vin, nBlockHeight, MIN_INSTANTX_PROTO_VERSION);

	if(n == -1)
	{
		LogPrint("instantx", "InstantX::DoConsensusVote - Unknown Masternode\n");
		
		return;
	}

	if(n > INSTANTX_SIGNATURES_TOTAL)
	{
		LogPrint("instantx", "InstantX::DoConsensusVote - Masternode not in the top %d (%d)\n", INSTANTX_SIGNATURES_TOTAL, n);
		
		return;
	}
	
	/*
		nBlockHeight calculated from the transaction is the authoritive source
	*/
	LogPrint("instantx", "InstantX::DoConsensusVote - In the top %d (%d)\n", INSTANTX_SIGNATURES_TOTAL, n);

	CConsensusVote ctx;
	
	ctx.vinMasternode = activeMasternode.vin;
	ctx.txHash = tx.GetHash();
	ctx.nBlockHeight = nBlockHeight;
	
	if(!ctx.Sign())
	{
		LogPrintf("InstantX::DoConsensusVote - Failed to sign consensus vote\n");
		
		return;
	}
	
	if(!ctx.SignatureValid())
	{
		LogPrintf("InstantX::DoConsensusVote - Signature invalid\n");
		
		return;
	}

	mapTxLockVote[ctx.GetHash()] = ctx;

	CInv inv(MSG_TXLOCK_VOTE, ctx.GetHash());

	RelayInventory(inv);
}

//received a consensus vote
bool ProcessConsensusVote(CNode* pnode, CConsensusVote& ctx)
{
	int n = mnodeman.GetMasternodeRank(ctx.vinMasternode, ctx.nBlockHeight, MIN_INSTANTX_PROTO_VERSION);

	CMasternode* pmn = mnodeman.Find(ctx.vinMasternode);
	
	if(pmn != NULL)
	{
		LogPrint("instantx", "InstantX::ProcessConsensusVote - Masternode ADDR %s %d\n", pmn->addr.ToString().c_str(), n);
	}

	if(n == -1)
	{
		//can be caused by past versions trying to vote with an invalid protocol
		LogPrint("instantx", "InstantX::ProcessConsensusVote - Unknown Masternode\n");
		
		mnodeman.AskForMN(pnode, ctx.vinMasternode);
		
		return false;
	}

	if(n > INSTANTX_SIGNATURES_TOTAL)
	{
		LogPrint("instantx", "InstantX::ProcessConsensusVote - Masternode not in the top %d (%d) - %s\n", INSTANTX_SIGNATURES_TOTAL, n, ctx.GetHash().ToString().c_str());
		
		return false;
	}

	if(!ctx.SignatureValid())
	{
		LogPrintf("InstantX::ProcessConsensusVote - Signature invalid\n");
		
		//don't ban, it could just be a non-synced masternode
		mnodeman.AskForMN(pnode, ctx.vinMasternode);
		
		return false;
	}

	if (!mapTxLocks.count(ctx.txHash))
	{
		LogPrintf("InstantX::ProcessConsensusVote - New Transaction Lock %s !\n", ctx.txHash.ToString().c_str());

		CTransactionLock newLock;
		
		newLock.nBlockHeight = 0;
		newLock.nExpiration = GetTime()+(20*60);
		newLock.nTimeout = GetTime()+(60*5);
		newLock.txHash = ctx.txHash;
		
		mapTxLocks.insert(std::make_pair(ctx.txHash, newLock));
	}
	else
	{
		LogPrint("instantx", "InstantX::ProcessConsensusVote - Transaction Lock Exists %s !\n", ctx.txHash.ToString().c_str());
	}

	CBlockIndex* pindex;
	CBlock block;
	CTxDB txdb("r");
	
	//compile consessus vote
	std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(ctx.txHash);
	
	if (i != mapTxLocks.end())
	{
		(*i).second.AddSignature(ctx);

#ifdef ENABLE_WALLET
		if(pwalletMain)
		{
			//when we get back signatures, we'll count them as requests. Otherwise the client will think it didn't propagate.
			if(pwalletMain->mapRequestCount.count(ctx.txHash))
			{
				pwalletMain->mapRequestCount[ctx.txHash]++;
			}
		}
#endif

		LogPrint("instantx", "InstantX::ProcessConsensusVote - Transaction Lock Votes %d - %s !\n", (*i).second.CountSignatures(), ctx.GetHash().ToString().c_str());

		if((*i).second.CountSignatures() >= INSTANTX_SIGNATURES_REQUIRED)
		{
			LogPrint(
				"instantx", "InstantX::ProcessConsensusVote - Transaction Lock Is Complete %s !\n",
				(*i).second.GetHash().ToString().c_str()
			);

			CTransaction& tx = mapTxLockReq[ctx.txHash];
			
			if(!CheckForConflictingLocks(tx))
			{
#ifdef ENABLE_WALLET
				if(pwalletMain)
				{
					if(pwalletMain->UpdatedTransaction((*i).second.txHash))
					{
						nCompleteTXLocks++;
					}
				}
#endif

				if(mapTxLockReq.count(ctx.txHash))
				{
					for(const CTxIn& in : tx.vin)
					{
						if(!mapLockedInputs.count(in.prevout))
						{
							mapLockedInputs.insert(std::make_pair(in.prevout, ctx.txHash));
						}
					}
				}

				// resolve conflicts

				//if this tx lock was rejected, we need to remove the conflicting blocks
				if(mapTxLockReqRejected.count((*i).second.txHash))
				{
					//reprocess the last 15 blocks
					block.DisconnectBlock(txdb, pindex);
					tx.DisconnectInputs(txdb);
				}
			}
		}
		
		return true;
	}

	return false;
}

bool CheckForConflictingLocks(CTransaction& tx)
{
	/*
		It's possible (very unlikely though) to get 2 conflicting transaction locks approved by the network.
		In that case, they will cancel each other out.

		Blocks could have been rejected during this time, which is OK. After they cancel out, the client will
		rescan the blocks and find they're acceptable and then take the chain with the most work.
	*/
	for(const CTxIn& in : tx.vin)
	{
		if(mapLockedInputs.count(in.prevout))
		{
			if(mapLockedInputs[in.prevout] != tx.GetHash())
			{
				LogPrintf("InstantX::CheckForConflictingLocks - found two complete conflicting locks - removing both. %s %s", tx.GetHash().ToString().c_str(), mapLockedInputs[in.prevout].ToString().c_str());
				
				if(mapTxLocks.count(tx.GetHash()))
				{
					mapTxLocks[tx.GetHash()].nExpiration = GetTime();
				}
				
				if(mapTxLocks.count(mapLockedInputs[in.prevout]))
				{
					mapTxLocks[mapLockedInputs[in.prevout]].nExpiration = GetTime();
				}
				
				return true;
			}
		}
	}

	return false;
}

int64_t GetAverageVoteTime()
{
	int64_t total = 0;
	int64_t count = 0;

	for(std::map<uint256, int64_t>::iterator it = mapUnknownVotes.begin(); it != mapUnknownVotes.end(); it++)
	{
		total += it->second;
		count++;
	}

	return total / count;
}

void CleanTransactionLocksList()
{
	if(pindexBest == NULL)
	{
		return;
	}

	std::map<uint256, CTransactionLock>::iterator it = mapTxLocks.begin();

	while(it != mapTxLocks.end())
	{
		if(GetTime() > it->second.nExpiration)
		{ //keep them for an hour
			LogPrintf("Removing old transaction lock %s\n", it->second.txHash.ToString().c_str());

			if(mapTxLockReq.count(it->second.txHash))
			{
				CTransaction& tx = mapTxLockReq[it->second.txHash];

				for(const CTxIn& in : tx.vin)
				{
					mapLockedInputs.erase(in.prevout);
				}
				
				mapTxLockReq.erase(it->second.txHash);
				mapTxLockReqRejected.erase(it->second.txHash);

				for(CConsensusVote& v : it->second.vecConsensusVotes)
				{
					mapTxLockVote.erase(v.GetHash());
				}
			}

			mapTxLocks.erase(it++);
		}
		else
		{
			it++;
		}
	}
}

