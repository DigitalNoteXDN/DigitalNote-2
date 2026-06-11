#include "compat.h"

#include <atomic>
#include <memory>
#include <openssl/sha.h>
#include <boost/tuple/tuple.hpp>

#include "blockparams.h"
#include "mining.h"
#include "txdb-leveldb.h"
#include "kernel.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "cmasternodepayments.h"
#include "masternodeman.h"
#include "masternode_extern.h"
#include "fork.h"
#include "cblock.h"
#include "cmasternodevotetracker.h"
#include "creservekey.h"
#include "cwallet.h"
#include "cwallettx.h"
#include "script.h"
#include "net.h"
#include "main_const.h"
#include "ctxmempool.h"
#include "ctxout.h"
#include "ctransaction.h"
#include "main_extern.h"
#include "cbitcoinaddress.h"
#include "chainparams.h"
#include "cchainparams.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "thread.h"
#include "util.h"
#include "cblockindex.h"
#include "ctxindex.h"
#include "enums/serialize_type.h"
#include "serialize.h"

#include "miner.h"

//////////////////////////////////////////////////////////////////////////////
//
// DigitalNoteMiner
//

extern unsigned int nMinerSleep;

int static FormatHashBlocks(void* pbuffer, unsigned int len)
{
	unsigned char* pdata = (unsigned char*)pbuffer;
	unsigned int blocks = 1 + ((len + 8) / 64);
	unsigned char* pend = pdata + 64 * blocks;

	memset(pdata + len, 0, 64 * blocks - len);

	pdata[len] = 0x80;
	
	unsigned int bits = len * 8;
	
	pend[-1] = (bits >> 0) & 0xff;
	pend[-2] = (bits >> 8) & 0xff;
	pend[-3] = (bits >> 16) & 0xff;
	pend[-4] = (bits >> 24) & 0xff;
	
	return blocks;
}

static const unsigned int pSHA256InitState[8] =
{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

void SHA256Transform(void* pstate, void* pinput, const void* pinit)
{
	SHA256_CTX ctx;
	unsigned char data[64];

	SHA256_Init(&ctx);

	for (int i = 0; i < 16; i++)
	{
		((uint32_t*)data)[i] = ByteReverse(((uint32_t*)pinput)[i]);
	}

	for (int i = 0; i < 8; i++)
	{
		ctx.h[i] = ((uint32_t*)pinit)[i];
	}

	SHA256_Update(&ctx, data, sizeof(data));

	for (int i = 0; i < 8; i++)
	{
		((uint32_t*)pstate)[i] = ctx.h[i];
	}
}

// Some explaining would be appreciated
class COrphan
{
public:
	CTransaction* ptx;
	std::set<uint256> setDependsOn;
	double dPriority;
	double dFeePerKb;

	COrphan(CTransaction* ptxIn)
	{
		ptx = ptxIn;
		dPriority = dFeePerKb = 0;
	}
};


uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;
int64_t nLastCoinStakeSearchInterval = 0;

// v2.0.0.8 CW2: GUI staking-icon state-machine support.  Two atomics
// published by ThreadStakeMiner, read by DigitalNoteGUI::updateStakingIcon().
// std::atomic so the GUI thread can read without holding any miner lock.
//
// nLastStakeLoopTime: GetTime() snapshot at the top of each
// ThreadStakeMiner main-loop iteration.  GUI uses (GetTime() - this) to
// detect a hung staker thread.  30s freshness window initially; once the
// icon latches on Hammer, the floor relaxes to 5 minutes.
//
// fLastStakeLoopProductive: TRUE iff the most recent main-loop iteration
// entered the SignBlock attempt path (all prerequisites met, kernel
// search executed).  FALSE for any branch that short-circuited (wallet
// locked, vNodes-empty/IBD, fTryToSync early-out, §29 voted-consensus
// defer, velocity-spacing back-off).  Set unconditionally at every
// branch decision so it always reflects the most recent iteration.
std::atomic<int64_t> nLastStakeLoopTime(0);
std::atomic<bool> fLastStakeLoopProductive(false);
 
// We want to sort transactions by priority and fee, so:
typedef boost::tuple<double, double, CTransaction*> TxPriority;

class TxPriorityCompare
{
	bool byFee;

public:
	TxPriorityCompare(bool _byFee) : byFee(_byFee) { }
	
	bool operator()(const TxPriority& a, const TxPriority& b)
	{
		if (byFee)
		{
			if (a.get<1>() == b.get<1>())
			{
				return a.get<0>() < b.get<0>();
			}
			
			return a.get<1>() < b.get<1>();
		}
		else
		{
			if (a.get<0>() == b.get<0>())
			{
				return a.get<1>() < b.get<1>();
			}
			
			return a.get<0>() < b.get<0>();
		}
	}
};

// CreateNewBlock: create new block (without proof-of-work/proof-of-stake)
CBlock* CreateNewBlock(CReserveKey& reservekey, bool fProofOfStake, int64_t* pFees)
{
	// Create new block
	CBlockPtr pblock(new CBlock());

	if (!pblock.get())
	{
		return NULL;
	}

	CBlockIndex* pindexPrev = pindexBest;
	int nHeight = pindexPrev->nHeight + 1;

	// Create coinbase tx
	CTransaction txNew;
	txNew.vin.resize(1);
	txNew.vin[0].prevout.SetNull();
	//txNew.vin[0].scriptSig = CScript() << nHeight;
	txNew.vout.resize(1);

	if (!fProofOfStake)
	{
		CPubKey pubkey;
		
		if (!reservekey.GetReservedKey(pubkey))
		{
			return NULL;
		}
		
		txNew.vout[0].scriptPubKey.SetDestination(pubkey.GetID());
	}	
	else
	{
		// Height first in coinbase required for block.version=2
		txNew.vin[0].scriptSig = (CScript() << nHeight) + COINBASE_FLAGS;
		assert(txNew.vin[0].scriptSig.size() <= 100);

		txNew.vout[0].SetEmpty();
	}

	// Add our coinbase tx as first transaction
	pblock->vtx.push_back(txNew);

	// Largest block you're willing to create:
	unsigned int nBlockMaxSize = GetArg("-blockmaxsize", MAX_BLOCK_SIZE_GEN/2);
	// Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
	nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MAX_BLOCK_SIZE-1000), nBlockMaxSize));

	// How much of the block should be dedicated to high-priority transactions,
	// included regardless of the fees they pay
	unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE);
	nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

	// Minimum block size you want to create; block will be filled with free transactions
	// until there are no more or the block reaches this size:
	unsigned int nBlockMinSize = GetArg("-blockminsize", 0);
	nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

	// Fee-per-kilobyte amount considered the same as "free"
	// Be careful setting this: if you set it to zero then
	// a transaction spammer can cheaply fill blocks using
	// 1-satoshi-fee transactions. It should be set above the real
	// cost to you of processing a transaction.
	int64_t nMinTxFee = MIN_TX_FEE;
	if (mapArgs.count("-mintxfee"))
	{
		ParseMoney(mapArgs["-mintxfee"], nMinTxFee);
	}

	// v2.0.0.8 CW7: nBits is computed AFTER pblock->nTime is finalized
	// below.  See the assignment site near pblock->nTime for the rationale.

	// Collect memory pool transactions into the block
	int64_t nFees = 0;

	{
		LOCK2(cs_main, mempool.cs);
		
		CTxDB txdb("r");
	//> XDN <
		// Priority order to process transactions
		std::list<COrphan> vOrphan; // list memory doesn't move
		std::map<uint256, std::vector<COrphan*> > mapDependers;

		// This vector will be sorted into a priority queue:
		std::vector<TxPriority> vecPriority;
		vecPriority.reserve(mempool.mapTx.size());
		
		for (std::map<uint256, CTransaction>::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi)
		{
			CTransaction& tx = (*mi).second;
			
			if (tx.IsCoinBase() || tx.IsCoinStake() || !IsFinalTx(tx, nHeight))
			{
				continue;
			}
			
			COrphan* porphan = NULL;
			double dPriority = 0;
			int64_t nTotalIn = 0;
			bool fMissingInputs = false;
			
			for(const CTxIn& txin : tx.vin)
			{
				// Read prev transaction
				CTransaction txPrev;
				CTxIndex txindex;
				
				if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
				{
					#ifdef ENABLE_ORPHAN_TRANSACTIONS
						// This should never happen; all transactions in the memory
						// pool should connect to either transactions in the chain
						// or other transactions in the memory pool.
						if (!mempool.mapTx.count(txin.prevout.hash))
						{
							LogPrintf("ERROR: mempool transaction missing input\n");
							
							if (fDebug)
							{
								assert("mempool transaction missing input" == 0);
							}
							
							fMissingInputs = true;
							
							if (porphan)
							{
								vOrphan.pop_back();
							}
							
							break;
						}

						// Has to wait for dependencies
						if (!porphan)
						{
							// Use list for automatic deletion
							vOrphan.push_back(COrphan(&tx));
							porphan = &vOrphan.back();
						}
						
						mapDependers[txin.prevout.hash].push_back(porphan);
						porphan->setDependsOn.insert(txin.prevout.hash);
						nTotalIn += mempool.mapTx[txin.prevout.hash].vout[txin.prevout.n].nValue;
					#else // ENABLE_ORPHAN_TRANSACTIONS
						fMissingInputs = true;
					#endif // ENABLE_ORPHAN_TRANSACTIONS
					
					continue;
				}
				
				int64_t nValueIn = txPrev.vout[txin.prevout.n].nValue;
				nTotalIn += nValueIn;

				int nConf = txindex.GetDepthInMainChain();
				dPriority += (double)nValueIn * nConf;
			}
			
			if (fMissingInputs)
			{
				continue;
			}
			
			// Priority is sum(valuein * age) / txsize
			unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
			dPriority /= nTxSize;

			// This is a more accurate fee-per-kilobyte than is used by the client code, because the
			// client code rounds up the size to the nearest 1K. That's good, because it gives an
			// incentive to create smaller transactions.
			double dFeePerKb =  double(nTotalIn-tx.GetValueOut()) / (double(nTxSize)/1000.0);
			
			#ifdef ENABLE_ORPHAN_TRANSACTIONS
				if (porphan)
				{
					porphan->dPriority = dPriority;
					porphan->dFeePerKb = dFeePerKb;
				}
				else
				{
					vecPriority.push_back(TxPriority(dPriority, dFeePerKb, &(*mi).second));
				}
			#else // ENABLE_ORPHAN_TRANSACTIONS
				vecPriority.push_back(TxPriority(dPriority, dFeePerKb, &(*mi).second));
			#endif // ENABLE_ORPHAN_TRANSACTIONS
		}

		// Collect transactions into block
		std::map<uint256, CTxIndex> mapTestPool;
		uint64_t nBlockSize = 1000;
		uint64_t nBlockTx = 0;
		int nBlockSigOps = 100;
		bool fSortedByFee = (nBlockPrioritySize <= 0);

		TxPriorityCompare comparer(fSortedByFee);
		std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);

		while (!vecPriority.empty())
		{
			// Take highest priority transaction off the priority queue:
			double dPriority = vecPriority.front().get<0>();
			double dFeePerKb = vecPriority.front().get<1>();
			CTransaction& tx = *(vecPriority.front().get<2>());

			std::pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
			vecPriority.pop_back();

			// Size limits
			unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
			if (nBlockSize + nTxSize >= nBlockMaxSize)
			{
				continue;
			}
			
			// Legacy limits on sigOps:
			unsigned int nTxSigOps = GetLegacySigOpCount(tx);
			if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
			{
				continue;
			}
			
			// Timestamp limit
			if (tx.nTime > GetAdjustedTime() || (fProofOfStake && tx.nTime > pblock->vtx[0].nTime))
			{
				continue;
			}
			
			// Skip free transactions if we're past the minimum block size:
			if (fSortedByFee && (dFeePerKb < nMinTxFee) && (nBlockSize + nTxSize >= nBlockMinSize))
			{
				continue;
			}
			
			// Prioritize by fee once past the priority size or we run out of high-priority
			// transactions:
			if (!fSortedByFee &&
				((nBlockSize + nTxSize >= nBlockPrioritySize) || (dPriority < COIN * 144 / 250)))
			{
				fSortedByFee = true;
				comparer = TxPriorityCompare(fSortedByFee);
				std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
			}

			// Connecting shouldn't fail due to dependency on other memory pool transactions
			// because we're already processing them in order of dependency
			std::map<uint256, CTxIndex> mapTestPoolTmp(mapTestPool);
			mapPrevTx_t mapInputs;
			bool fInvalid;
			
			if (!tx.FetchInputs(txdb, mapTestPoolTmp, false, true, mapInputs, fInvalid))
			{
				continue;
			}
			
			int64_t nTxFees = tx.GetValueMapIn(mapInputs)-tx.GetValueOut();

			nTxSigOps += GetP2SHSigOpCount(tx, mapInputs);
			
			if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
			{
				continue;
			}
			
			// Note that flags: we don't want to set mempool/IsStandard()
			// policy here, but we still have to ensure that the block we
			// create only contains transactions that are valid in new blocks.
			if (!tx.ConnectInputs(txdb, mapInputs, mapTestPoolTmp, CDiskTxPos(1,1,1), pindexPrev, false, true, MANDATORY_SCRIPT_VERIFY_FLAGS))
			{
				continue;
			}
			
			mapTestPoolTmp[tx.GetHash()] = CTxIndex(CDiskTxPos(1,1,1), tx.vout.size());
			swap(mapTestPool, mapTestPoolTmp);

			// Added
			pblock->vtx.push_back(tx);
			nBlockSize += nTxSize;
			++nBlockTx;
			nBlockSigOps += nTxSigOps;
			nFees += nTxFees;

			if (fDebug && GetBoolArg("-printpriority", false))
			{
				LogPrintf(
					"priority %.1f feeperkb %.1f txid %s\n",
					dPriority,
					dFeePerKb,
					tx.GetHash().ToString()
				);
			}

			// Add transactions that depend on this one to the priority queue
			uint256 hash = tx.GetHash();
			if (mapDependers.count(hash))
			{
				for(COrphan* porphan : mapDependers[hash])
				{
					if (!porphan->setDependsOn.empty())
					{
						porphan->setDependsOn.erase(hash);
						
						if (porphan->setDependsOn.empty())
						{
							vecPriority.push_back(TxPriority(porphan->dPriority, porphan->dFeePerKb, porphan->ptx));
							
							std::push_heap(vecPriority.begin(), vecPriority.end(), comparer);
						}
					}
				}
			}
		}

		nLastBlockTx = nBlockTx;
		nLastBlockSize = nBlockSize;

		if (fDebug && GetBoolArg("-printpriority", false))
		{
			LogPrintf("CreateNewBlock(): total size %u\n", nBlockSize);
		}
		
		// > XDN <
		if (!fProofOfStake)
		{
			pblock->vtx[0].vout[0].nValue = GetProofOfWorkReward(pindexPrev->nHeight + 1, nFees);
			
			int64_t block_time = pindexBest->GetBlockTime();
			
			// Check for payment update fork
			if(block_time > 0)
			{
				// Testnet always requires MN/devops payments from genesis.
				// Mainnet uses the May-2019 fork-date gate as the cutoff.
				if(block_time > VERION_1_0_1_5_MANDATORY_UPDATE_START || TestNet())
				{
					// masternode/devops payment
					int64_t blockReward = GetProofOfWorkReward(pindexPrev->nHeight + 1, nFees);
					bool hasPayment = true;
					bool bMasterNodePayment = true;// TODO: Setup proper network toggle
					CScript mn_payee;
					CScript do_payee;
					CTxIn vin;

					// Determine our payment address for devops
					//
					// OLD IMPLEMENTATION COMMNETED OUT
					// CScript devopsScript;
					// devopsScript << OP_DUP << OP_HASH160 << ParseHex(Params().DevOpsPubKey()) << OP_EQUALVERIFY << OP_CHECKSIG;
					// do_payee = devopsScript;
					//
					// Define Address
					//
					// TODO: Clean this up, it's a mess (could be done much more cleanly)
					//       Not an issue otherwise, merely a pet peev. Done in a rush...
					//
					// v2.0.0.8 CW9: route both mainnet and testnet through the
					// height-based ladder.  Testnet handling is now inside the
					// ladder (so the v2.0.1.0 testnet rotation at block 100
					// fires correctly here).  Producer asks the ladder about
					// the height of the block being mined (pindexBest->nHeight
					// + 1), not the tip -- this is the off-by-one fix that
					// closes the longstanding producer/validator disagreement
					// at rotation boundaries.
					CBitcoinAddress devopaddress;
					if (Params().NetworkID() == CChainParams_Network::MAIN ||
					    Params().NetworkID() == CChainParams_Network::TESTNET)
					{
						devopaddress = CBitcoinAddress(
							getDevelopersAdressForHeight(
								pindexBest->nHeight + 1,
								GetAdjustedTime()
							)
						);
					}
					else if (Params().NetworkID() == CChainParams_Network::REGTEST)
					{
						devopaddress = CBitcoinAddress("");
					}

					// verify address
					if(devopaddress.IsValid())
					{
						//spork
						if(pindexBest->GetBlockTime() > 1546123500) // ON  (Saturday, December 29, 2018 10:45 PM)
						{
								do_payee = GetScriptForDestination(devopaddress.Get());
						}
						else
						{
							hasPayment = false;
						}
					}
					else
					{
						LogPrintf("CreateNewBlock(): Failed to detect dev address to pay\n");
					}

					if(bMasterNodePayment)
					{
												//spork
						// v2.0.0.8 M5: route through GetEnforcedPayee instead of
						// directly calling masternodePayments.GetBlockPayee.
						// Post-activation with consensus, this returns the
						// voted-consensus winner -- matching what the validator
						// (cblock.cpp via M4) will check against.  Otherwise
						// behaves identically to the previous direct call.
						if(!GetEnforcedPayee(pindexPrev->nHeight+1, mn_payee, vin))
						{
							// vWinning has no entry for the upcoming height -- fall back
							// to FindOldestNotInVec (same as ProcessBlock's secondary path).
							// The previous fallback used GetCurrentMasterNode(1), which
							// internally calls CalculateScore(1, blockHeight=0) -- the
							// genesis block hash -- and therefore always returned the same
							// MN as winner.  That produced the "same MN paid twice in
							// succession" pattern observed in UAT whenever a staker hit
							// the fallback path (typically just-restarted wallets or fresh
							// syncs).  Companion fix to rpcmining.cpp:847.
							CMasternode* pmn = mnodeman.FindOldestNotInVec(std::vector<CTxIn>(), 0);
							if(pmn)
							{
								mn_payee = GetScriptForDestination(pmn->pubkey.GetID());
							}
							else
							{
								mn_payee = do_payee;
							}
						}
						else
						{
							// v2.0.0.8 Mechanism 2: GetEnforcedPayee succeeded
							// (returned a consensus winner), but the winner may
							// have gone offline inside the recast window.  If the
							// builder cannot resolve the winner in its own MN
							// list -- AND the winner is not the devops fallback
							// address (which is itself a legitimate payee) --
							// demote to the legacy fallback chain by calling
							// masternodePayments.GetBlockPayee directly, exactly
							// as if consensus had not formed.  This is the same
							// path the post-activation no-consensus case already
							// uses (cblock.cpp:GetEnforcedPayee line 186).  The
							// existing cascade then picks a real alternative MN
							// if it can, or devops if it cannot.
							//
							// The builder's reachability check
							// (IsPayeeAValidMasternode + devops-address check) is
							// EXACTLY what the validator's weak check uses, so a
							// payee that passes here is guaranteed to pass
							// validation.  Predicate symmetry is the whole point
							// of this mechanism -- the 1892 stall was caused by
							// builder and validator using different definitions
							// of "valid MN", and this aligns them.
							CTxDestination addrDest;
							ExtractDestination(mn_payee, addrDest);
							CBitcoinAddress addrOut(addrDest);
							// v2.0.0.8 CW9: ask the ladder about the block being
							// mined (pindexPrev->nHeight + 1), not the tip.
							std::string strDevopsAddress = getDevelopersAdressForHeight(
								pindexPrev->nHeight + 1,
								GetAdjustedTime()
							);

							if (!mnodeman.IsPayeeAValidMasternode(mn_payee, pindexPrev->nHeight + 1) &&
								addrOut.ToString() != strDevopsAddress)
							{
								LogPrintf("NOTICE - voted consensus winner for height %d "
										  "(%s) is not in local list; falling back to "
										  "legacy payee selection\n",
										  pindexPrev->nHeight + 1,
										  addrOut.ToString().c_str());

								// Demote to the same legacy path GetEnforcedPayee
								// uses on no-consensus.  Reset and re-fill via the
								// same fallback structure as above.
								mn_payee = CScript();
								vin = CTxIn();

								if (!masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, mn_payee, vin))
								{
									CMasternode* pmn = mnodeman.FindOldestNotInVec(std::vector<CTxIn>(), 0);
									if(pmn)
									{
										mn_payee = GetScriptForDestination(pmn->pubkey.GetID());
									}
									else
									{
										mn_payee = do_payee;
									}
								}
							}
						}
					}
					else
					{
						hasPayment = false;
					}

					CAmount masternodePayment = GetMasternodePayment(nHeight, blockReward);
					CAmount devopsPayment = GetDevOpsPayment(nHeight, blockReward);

					if (hasPayment)
					{
						pblock->vtx[0].vout.resize(3);
						pblock->vtx[0].vout[1].scriptPubKey = mn_payee;
						pblock->vtx[0].vout[1].nValue = masternodePayment;
						pblock->vtx[0].vout[2].scriptPubKey = do_payee;
						pblock->vtx[0].vout[2].nValue = devopsPayment;
						pblock->vtx[0].vout[0].nValue = blockReward - (masternodePayment + devopsPayment);
					}

					CTxDestination address1;
					CTxDestination address3;
					ExtractDestination(mn_payee, address1);
					ExtractDestination(do_payee, address3);
					CBitcoinAddress address2(address1);
					CBitcoinAddress address4(address3);
					
					LogPrintf("CreateNewBlock(): Masternode payment %lld to %s\n",
						FormatMoney(masternodePayment),
						address2.ToString().c_str()
					);
					
					LogPrintf("CreateNewBlock(): Devops payment %lld to %s\n",
						FormatMoney(devopsPayment),
						address4.ToString().c_str()
					);
				}
			} //
		}

		if (pFees)
		{
			*pFees = nFees;
		}
		
		// Fill in header
		pblock->hashPrevBlock = pindexPrev->GetBlockHash();
		pblock->nTime = std::max(pindexPrev->GetPastTimeLimit()+1, pblock->GetMaxTransactionTime());
		
		if (!fProofOfStake)
		{
			pblock->UpdateTime(pindexPrev);
		}
		
		// v2.0.0.8 CW7: compute nBits AFTER pblock->nTime is finalized so
		// the miner uses the exact same time input the validator will use.
		//
		// AcceptBlock recomputes the expected nBits by calling
		// GetNextTargetRequired(pindexPrev, IsProofOfStake(), GetBlockTime())
		// where GetBlockTime() returns the committed pblock->nTime.  By
		// computing nBits here using pblock->nTime, miner and validator
		// pass byte-identical input to VRX_ThreadCurve's recovery loop and
		// therefore arrive at byte-identical nBits.
		//
		// Pre-CW7 the miner computed nBits early in CreateNewBlock using
		// GetAdjustedTime() ~milliseconds-to-seconds before pblock->nTime
		// was finalized.  For most blocks the two timestamps round to the
		// same answer through the recovery loop's hourly boundaries (3600,
		// 7200, 10800, 14400, 18000 seconds).  For stall-recovery blocks
		// whose wall-clock delta from the previous block straddles one of
		// those boundaries, the few-second gap could push the validator's
		// computed delta across, producing a different nBits and a
		// self-validation reject on submission.  CW7 eliminates that
		// edge case entirely.
		//
		// Safe to move: pblock->nBits is not read between its prior
		// (now-removed) assignment site and this line.  Block reward
		// calculation (GetProofOfWorkReward / GetProofOfStakeReward) takes
		// height/coin-age/fees, not nBits.  Coinstake reward is computed
		// later in CWallet::CreateCoinStake, called after CreateNewBlock
		// returns.
		pblock->nBits = GetNextTargetRequired(pindexPrev, fProofOfStake, pblock->nTime);
		
		pblock->nNonce = 0;
	}

	return pblock.release();
}

void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
	// Update nExtraNonce
	static uint256 hashPrevBlock;
	if (hashPrevBlock != pblock->hashPrevBlock)
	{
		nExtraNonce = 0;
		hashPrevBlock = pblock->hashPrevBlock;
	}

	++nExtraNonce;

	unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
	pblock->vtx[0].vin[0].scriptSig = (CScript() << nHeight << CBigNum(nExtraNonce)) + COINBASE_FLAGS;

	assert(pblock->vtx[0].vin[0].scriptSig.size() <= 100);

	pblock->hashMerkleRoot = pblock->BuildMerkleTree();
}

void FormatHashBuffers(CBlock* pblock, char* pmidstate, char* pdata, char* phash1)
{
	//
	// Pre-build hash buffers
	//
	struct
	{
		struct unnamed2
		{
			int nVersion;
			uint256 hashPrevBlock;
			uint256 hashMerkleRoot;
			unsigned int nTime;
			unsigned int nBits;
			unsigned int nNonce;
		}
		block;
		unsigned char pchPadding0[64];
		uint256 hash1;
		unsigned char pchPadding1[64];
	}
	tmp;

	memset(&tmp, 0, sizeof(tmp));

	tmp.block.nVersion = pblock->nVersion;
	tmp.block.hashPrevBlock = pblock->hashPrevBlock;
	tmp.block.hashMerkleRoot = pblock->hashMerkleRoot;
	tmp.block.nTime = pblock->nTime;
	tmp.block.nBits = pblock->nBits;
	tmp.block.nNonce = pblock->nNonce;

	FormatHashBlocks(&tmp.block, sizeof(tmp.block));
	FormatHashBlocks(&tmp.hash1, sizeof(tmp.hash1));

	// Byte swap all the input buffer
	for (unsigned int i = 0; i < sizeof(tmp)/4; i++)
	{
		((unsigned int*)&tmp)[i] = ByteReverse(((unsigned int*)&tmp)[i]);
	}

	// Precalc the first half of the first hash, which stays constant
	SHA256Transform(pmidstate, &tmp.block, pSHA256InitState);

	memcpy(pdata, &tmp.block, 128);
	memcpy(phash1, &tmp.hash1, 64);
}

bool CheckWork(CBlock* pblock, CWallet& wallet, CReserveKey& reservekey)
{
	uint256 hashBlock = pblock->GetHash();
	uint256 hashProof = pblock->GetPoWHash();
	uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

	if(!pblock->IsProofOfWork())
	{
		return error("CheckWork() : %s is not a proof-of-work block", hashBlock.GetHex());
	}

	if (hashProof > hashTarget)
	{
		return error("CheckWork() : proof-of-work not meeting target");
	}

	//// debug print
	LogPrintf("CheckWork() : new proof-of-work block found  \n  proof hash: %s  \ntarget: %s\nblock hash: %s\ngenerated %s\n",
		hashProof.GetHex(),
		hashTarget.GetHex(),
		pblock->ToString(),
		FormatMoney(pblock->vtx[0].vout[0].nValue)
	);

	// Found a solution
	{
		LOCK(cs_main);
		
		if (pblock->hashPrevBlock != hashBestChain)
		{
			return error("CheckWork() : generated block is stale");
		}
		
		// Remove key from key pool
		reservekey.KeepKey();

		// Track how many getdata requests this block gets
		{
			LOCK(wallet.cs_wallet);
			
			wallet.mapRequestCount[hashBlock] = 0;
		}

		// Process this block the same as if we had received it from another node
		if (!ProcessBlock(NULL, pblock))
		{
			return error("CheckWork() : ProcessBlock, block not accepted");
		}
	}

	return true;
}

bool CheckStake(CBlock* pblock, CWallet& wallet)
{
	uint256 proofHash = 0, hashTarget = 0;
	uint256 hashBlock = pblock->GetHash();

	if(!pblock->IsProofOfStake())
	{
		return error("CheckStake() : %s is not a proof-of-stake block", hashBlock.GetHex());
	}

	// verify hash target and signature of coinstake tx
	if (!CheckProofOfStake(mapBlockIndex[pblock->hashPrevBlock], pblock->vtx[1], pblock->nBits, proofHash, hashTarget))
	{
		return error("CheckStake() : proof-of-stake checking failed");
	}

	//// debug print
	LogPrint("coinstake", "CheckStake() : new proof-of-stake block found  \n  hash: %s \nproofhash: %s  \ntarget: %s\nblock %s\nout %s\n",
		hashBlock.GetHex(),
		proofHash.GetHex(),
		hashTarget.GetHex(),
		pblock->ToString(),
		FormatMoney(pblock->vtx[1].GetValueOut())
	);

	// Found a solution
	{
		LOCK(cs_main);
		
		if (pblock->hashPrevBlock != hashBestChain)
		{
			return error("CheckStake() : generated block is stale");
		}
		
		// Track how many getdata requests this block gets
		{
			LOCK(wallet.cs_wallet);
			
			wallet.mapRequestCount[hashBlock] = 0;
		}

		// Process this block the same as if we had received it from another node
		if (!ProcessBlock(NULL, pblock))
		{
			return error("CheckStake() : ProcessBlock, block not accepted");
		}
		
		// v2.0.0.8 CW4 Fix B: targeted vfSpent maintenance for the just-
		// accepted PoS coinstake's inputs.  Pre-Fix-B this site called
		// `wallet.FixSpentCoins(...)`, an O(mapWallet) scan with one
		// LevelDB seek per wtx, firing on every PoS block (always Branch 2:
		// coinstake-input lifecycle update).  Empirical confirmation: a
		// parallel `repairwallet` test on a PoW miner returned "nothing
		// to do", so Branch 1 (lost-coin recovery) never accumulates --
		// FixSpentCoins was doing exactly one job, the per-PoS-block
		// coinstake-input MarkSpent.
		//
		// The targeted loop below walks only the coinstake's actual
		// inputs (O(vin.size()), typically 1-2 vs O(mapWallet)).  Pattern
		// mirrors SyncTransaction's fFixSpentCoins branch at
		// cwallet.cpp:2829-2844 and CommitTransaction at
		// cwallet.cpp:3712-3724.  Lock pattern matches SyncTransaction:
		// nested cs_wallet inside the already-held cs_main (LOCK2
		// equivalent for this scope) -- canonical lock order, no
		// deadlock risk.
		//
		// FixSpentCoins remains in the codebase as a manual-recovery
		// utility (repairwallet RPC + GUI "Repair Wallet" menu).
		{
			LOCK(wallet.cs_wallet);
			
			// pblock->vtx[1] is the PoS coinstake; vtx[0] is the empty
			// PoS coinbase per the PoS block-structure convention.
			const CTransaction& coinstake = pblock->vtx[1];
			int nMarked = 0;
			
			for (const CTxIn& txin : coinstake.vin)
			{
				mapWallet_t::iterator it =
					wallet.mapWallet.find(txin.prevout.hash);
				
				if (it == wallet.mapWallet.end())
				{
					// Input is not from this wallet (legal in
					// multi-wallet setups, or for stakes assembled
					// from inputs not all of which are ours).
					// No-op -- nothing to mark.
					continue;
				}
				
				CWalletTx& coin = it->second;
				
				coin.BindWallet(&wallet);
				coin.MarkSpent(txin.prevout.n);
				coin.WriteToDisk();
				
				nMarked++;
			}
			
			LogPrint("coinstake",
				"CheckStake Fix B: marked %d input(s) spent for "
				"coinstake %s in block %s\n",
				nMarked,
				coinstake.GetHash().ToString(),
				hashBlock.ToString()
			);
		}
	}

	return true;
}

void ThreadStakeMiner(CWallet *pwallet)
{
	SetThreadPriority(THREAD_PRIORITY_LOWEST);

	// Make this thread recognisable as the mining thread
	RenameThread("DigitalNote-miner");

	CReserveKey reservekey(pwallet);

	bool fTryToSync = true;

	while (true)
	{
		// CW2: heartbeat. Update once per outer-loop iteration, BEFORE any
		// branch decisions, so the GUI can detect a hung/dead staker.
		nLastStakeLoopTime.store(GetTime());

		while (pwallet->IsLocked())
		{
			nLastCoinStakeSearchInterval = 0;
			fLastStakeLoopProductive.store(false);
			MilliSleep(1000);
		}

		while (vNodes.empty() || IsInitialBlockDownload())
		{
			nLastCoinStakeSearchInterval = 0;
			fLastStakeLoopProductive.store(false);
			fTryToSync = true;
			MilliSleep(1000);
		}

		if (fTryToSync)
		{
			fTryToSync = false;
			
			if (vNodes.size() < 3 || pindexBest->GetBlockTime() < GetTime() - 10 * 60)
			{
				fLastStakeLoopProductive.store(false);
				MilliSleep(10000);
				
				continue;
			}
		}

		// ===================================================================
		// v2.0.0.8 Round 3 -- voted-consensus readiness gate.
		//
		// A node must NOT mint a block while voted consensus is active for
		// that block's height UNLESS it can actually produce the voted
		// payee.  If it cannot, CreateNewBlock -> GetEnforcedPayee falls
		// back to legacy GetBlockPayee and builds a block paying a payee
		// the rest of the (vote-aware) fleet will reject with
		// "Couldn't find masternode payment or payee" -- which is exactly
		// the 1412 fork: the staker had an empty vote tracker, minted on
		// the legacy payee, and every fleet node banned it.
		//
		// The gate mirrors GetEnforcedPayee's own logic precisely so the
		// producer and the validators agree:
		//
		//   nextHeight = pindexBest->nHeight + 1   (the block we'd build)
		//
		//   * nextHeight <  activationHeight  -> voted consensus NOT active.
		//     Legacy GetBlockPayee is the CORRECT payee source.  Do not
		//     gate -- mint normally.  (Gating here would wrongly freeze
		//     staking on every pre-activation chain.)
		//
		//   * nextHeight >= activationHeight  -> voted consensus IS active.
		//     The block MUST carry the voted payee.  Only mint if
		//     GetCanonicalWinner can supply one for nextHeight.  If it
		//     cannot (empty/sub-quorum vote tracker on this node), DEFER:
		//     sleep and retry rather than mint a block that will fork.
		//
		// Consequence to expect: a node whose vote tracker is not being
		// populated will stop staking and log the line below.  That is the
		// CORRECT, SAFE outcome -- a paused staker beats a forked chain.
		// Restoring staking on such a node is Round 4 (vote propagation),
		// NOT this gate.
		// ===================================================================
		{
			int nNextHeight = pindexBest->nHeight + 1;
			int nActivationHeight = GetEffectiveVotedConsensusActivationHeight();

			if (nNextHeight >= nActivationHeight)
			{
				CScript votedPayeeProbe;

				// M1Q: probe the QUEUE path -- the same source the validator
				// (cblock.cpp GetEnforcedPayee -> GetCanonicalWinnerFromQueues)
				// and enforcement use.  Step 4 switched the producer to
				// BroadcastQueue (main.cpp ~2258) and the validator to
				// GetCanonicalWinnerFromQueues, but this readiness gate was
				// left probing the now-dormant per-height GetCanonicalWinner
				// (mapVotes), which is no longer populated -> the gate
				// deferred forever and the staker never resumed after the
				// M1Q restart.  GetCanonicalWinnerFromQueues(tip+1) is the
				// correct producer/validator-agreeing probe: its commit-point
				// gate (currentTip < tip+1 - VOTE_COMMIT_BUFFER) is always
				// false for tip+1, and it resolves position 0 of the
				// queue broadcast at height tip.
				//
				// Lock order: cs_main FIRST, then voteTracker.cs.  This
				// matches every other path that reaches the queue tracker:
				// CheckBlock / GetEnforcedPayee runs under cs_main (cblock.cpp
				// AssertLockHeld:1013,1832); ProcessMessages takes cs_main
				// (main.cpp:3383) before AlreadyHave -> AlreadyHaveQueue.
				// Without this LOCK, the staker would hold voteTracker.cs
				// (taken inside GetCanonicalWinnerFromQueues) and then need
				// cs_main downstream via CountVotingEligible ->
				// IsVotingEligible -> GetCollateralConfirmedHeight ->
				// GetTransaction -- while ProcessMessages holds cs_main and
				// wants voteTracker.cs for AlreadyHaveQueue.  Classic ABBA
				// deadlock; observed live on the debug-build staker, gdb
				// stack-trace identified -- threads 18 (staker) and 10
				// (message handler) holding opposite locks each waiting on
				// the other, GUI main thread blocked acquiring cs_main from
				// updateStakingIcon -> IsInitialBlockDownload.
				//
				// CW0 (2026-05-30): cs_main MUST be released before the
				// MilliSleep below.  The original §24 fix placed the LOCK
				// without a fresh scope, so its lifetime extended through
				// the defer-and-retry path -- cs_main was held for the
				// full 5s sleep, starving ProcessMessages / GUI /
				// InitializeNode / FinalizeNode.  Under wedge conditions
				// (no fresh queues arriving) the next iteration deferred
				// again, reacquired cs_main, slept another 5s -- self-
				// sustaining starvation.  Observed live in the 24h soak
				// of 2026-05-30; gdb-on-live-process showed
				// locking_thread_id = ThreadStakeMiner and the scope-
				// variable criticalblock.is_locked = true at the
				// MilliSleep frame.  Fix: scope the LOCK strictly around
				// the call that needs it; the sleep runs unlocked.
				bool fHaveCanonical = false;
				{
					LOCK(cs_main);

					fHaveCanonical = voteTracker.GetCanonicalWinnerFromQueues(
							nNextHeight, votedPayeeProbe);
				}   // cs_main released here, BEFORE the sleep below

				if (!fHaveCanonical)
				{
					static int64_t nLastGateLog = 0;
					int64_t nNow = GetTime();

					// Rate-limit the log so a deferring node does not flood
					// debug.log (it retries every 5s).
					if (nNow - nLastGateLog >= 30)
					{
						nLastGateLog = nNow;
						LogPrintf("ThreadStakeMiner -- deferring: voted consensus "
								  "active for height %d (activation %d) but this "
								  "node has no canonical winner yet; not minting "
								  "a block the fleet would reject. Vote tracker "
								  "not ready.\n",
								  nNextHeight, nActivationHeight);
					}

					MilliSleep(5000);

					fLastStakeLoopProductive.store(false);
					continue;
				}
			}
		}

		// ===================================================================
		// v2.0.0.8 Velocity spacing back-off gate.
		//
		// THE STORM being fixed: Velocity() (velocity.cpp) enforces a
		// MINIMUM block spacing -- a block is rejected with
		// "DENIED: Minimum block spacing not met for Velocity" when
		//     block.GetBlockTime() - prevBlock.GetBlockTime() < BLOCK_SPACING_MIN
		// That rejection becomes DoS(100) in AcceptBlock, ProcessBlock
		// fails, and CheckStake returns false.
		//
		// The stake loop, however, ignored CheckStake's result and slept
		// only 500ms before re-running CreateNewBlock.  If the staker had
		// found a valid kernel but the chain tip was younger than
		// BLOCK_SPACING_MIN seconds, every retry produced the SAME
		// too-early block, got Velocity-rejected again, and span -- a
		// tight 500ms CPU/log storm of "DENIED: Minimum block spacing"
		// until wall-clock finally crossed the threshold.
		//
		// THE FIX: the spacing rule is fully deterministic and the staker
		// knows every input.  The earliest timestamp a new block can carry
		// and still pass Velocity is:
		//     nEarliestValid = tipTime + BLOCK_SPACING_MIN
		// If that is still in the future, NO amount of retrying will
		// produce an acceptable block before then.  So sleep until then
		// (plus a 1s margin) instead of spinning.  This removes the storm
		// at its source: a too-early block is never attempted, so Velocity
		// never rejects one for spacing.
		//
		// Pre-condition note: pindexBest is non-NULL here -- the loop has
		// already passed the IsInitialBlockDownload()/vNodes guards above.
		// ===================================================================
		{
			int64_t nTipTime        = pindexBest->GetBlockTime();
			int64_t nEarliestValid  = nTipTime + BLOCK_SPACING_MIN;
			int64_t nNow            = GetAdjustedTime();

			if (nNow < nEarliestValid)
			{
				int64_t nWaitSecs = (nEarliestValid - nNow) + 1; // +1s margin

				// Cap a single sleep so the loop still re-checks wallet
				// lock / sync / the Round-3 gate periodically rather than
				// committing to one long uninterruptible sleep.
				if (nWaitSecs > 30)
				{
					nWaitSecs = 30;
				}

				static int64_t nLastSpacingLog = 0;

				// Rate-limit: log at most every 30s so a node waiting out
				// the spacing window does not flood debug.log.
				if (nNow - nLastSpacingLog >= 30)
				{
					nLastSpacingLog = nNow;
					LogPrintf("ThreadStakeMiner -- waiting for Velocity min "
							  "block spacing: tip at %d, earliest valid block "
							  "time %d, %d s to go\n",
							  (int64_t)nTipTime, (int64_t)nEarliestValid,
							  (int64_t)(nEarliestValid - nNow));
				}

				MilliSleep(nWaitSecs * 1000);

				fLastStakeLoopProductive.store(false);
				continue;
			}
		}

		//
		// Create new block
		//
		int64_t nFees;
		CBlockPtr pblock(CreateNewBlock(reservekey, true, &nFees));
		
		if (!pblock.get())
		{
			return;
		}
		
		// CW2: this is the single productive path -- all prerequisites
		// passed, the SignBlock attempt is running.  Set the flag BEFORE
		// the call so the GUI sees the wallet as "staking" even during
		// the kernel search (which on a low-weight wallet can be the
		// dominant time fraction).
		fLastStakeLoopProductive.store(true);

		// Trying to sign a block
		if (pblock->SignBlock(*pwallet, nFees))
		{
			SetThreadPriority(THREAD_PRIORITY_NORMAL);

			// v2.0.0.8 Velocity storm fix Part 2: capture CheckStake's
			// result.  The loop previously discarded it and slept a fixed
			// 500ms regardless -- so a block rejected by ProcessBlock (for
			// ANY reason: Velocity spacing, stale tip, etc.) was retried
			// almost immediately.  The spacing case is now prevented by the
			// back-off gate above, but other rejections still warrant a
			// longer pause than the 500ms success delay rather than an
			// instant re-attempt of a block that just failed.
			bool fStakeAccepted = CheckStake(pblock.get(), *pwallet);

			SetThreadPriority(THREAD_PRIORITY_LOWEST);

			if (fStakeAccepted)
			{
				// Block accepted -- brief pause, then look for the next.
				MilliSleep(500);
			}
			else
			{
				// Block was signed but not accepted.  Back off the normal
				// miner interval instead of spinning on the same failure.
				MilliSleep(nMinerSleep);
			}
		}
		else
		{
			MilliSleep(nMinerSleep);
		}
	}
}

