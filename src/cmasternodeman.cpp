#include "compat.h"

#include <boost/lexical_cast.hpp>

#include "main.h"
#include "cblock.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "mining.h"
#include "caddrman.h"
#include "cblockindex.h"
#include "cvalidationstate.h"
#include "net.h"
#include "net/cnode.h"
#include "util.h"
#include "ui_interface.h"
#include "serialize.h"
#include "cmasternode.h"
#include "cmasternodepayments.h"
#include "cmasternodevotetracker.h"
#include "cactivemasternode.h"
#include "masternode.h"
#include "masternodeman.h"
#include "masternode_extern.h"
#include "comparevalueonly.h"
#include "main_extern.h"
#include "ctxout.h"
#include "cmnenginesigner.h"
#include "cmnenginepool.h"
#include "mnengine_extern.h"
#include "script.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "thread.h"

#include "cmasternodeman.h"

/** Masternode manager */
CCriticalSection cs_process_message;

CMasternodeMan::CMasternodeMan()
{
	nDsqCount = 0;
	nLastPaidHeightScannedTo = 0;
}

bool CMasternodeMan::Add(CMasternode &mn)
{
	LOCK(cs);

	if (!mn.IsEnabled())
	{
		return false;
	}

	CMasternode *pmn = Find(mn.vin);

	if (pmn == NULL)
	{
		LogPrint("masternode", "CMasternodeMan: Adding new masternode %s - %i now\n", mn.addr.ToString().c_str(), size() + 1);
		
		vMasternodes.push_back(mn);

		// v2.0.0.8 M3 patch 4: populate this newly-added MN's cache entry
		// from chain history.  Without this, MNs that join via dsee after the
		// startup PopulateLastPaidHeightCache run stay at paidHeight=0 forever
		// and get incorrectly selected as "longest-ago-paid."  See UAT-followup U3.
		//
		// RecomputeLastPaidHeight does a bounded walk; in steady-state runtime
		// it only walks back to nLastPaidHeightScannedTo (a recent point), so
		// the per-Add cost is small.  Safe to call under cs since
		// CCriticalSection is recursive.
		CMasternode &added = vMasternodes.back();
		RecomputeLastPaidHeight(&added);

		return true;
	}

	return false;
}

void CMasternodeMan::AskForMN(CNode* pnode, CTxIn &vin)
{
	std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasternodeListEntry.find(vin.prevout);

	if (i != mWeAskedForMasternodeListEntry.end())
	{
		int64_t t = (*i).second;
		
		if (GetTime() < t)
		{
			return; // we've asked recently
		}
	}

	// ask for the mnb info once from the node that sent mnp
	LogPrintf("CMasternodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.ToString());

	pnode->PushMessage("dseg", vin);

	int64_t askAgain = GetTime() + MASTERNODE_MIN_DSEEP_SECONDS;

	mWeAskedForMasternodeListEntry[vin.prevout] = askAgain;
}

void CMasternodeMan::Check()
{
	LOCK(cs);

	for(CMasternode& mn : vMasternodes)
	{
		mn.Check();
	}
}

void CMasternodeMan::CheckAndRemove()
{
	LOCK(cs);

	Check();

	//remove inactive
	std::vector<CMasternode>::iterator it = vMasternodes.begin();
	while(it != vMasternodes.end())
	{
		if(
			(*it).activeState == CMasternode::MASTERNODE_REMOVE ||
			(*it).activeState == CMasternode::MASTERNODE_VIN_SPENT ||
			(*it).protocolVersion < nMasternodeMinProtocol
		)
		{
			LogPrint("masternode", "CMasternodeMan: Removing inactive masternode %s - %i now\n", (*it).addr.ToString().c_str(), size() - 1);
			
			it = vMasternodes.erase(it);
		}
		else
		{
			++it;
		}
	}

	// check who's asked for the masternode list
	std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForMasternodeList.begin();
	while(it1 != mAskedUsForMasternodeList.end())
	{
		if((*it1).second < GetTime())
		{
			mAskedUsForMasternodeList.erase(it1++);
		}
		else
		{
			++it1;
		}
	}

	// check who we asked for the masternode list
	it1 = mWeAskedForMasternodeList.begin();
	while(it1 != mWeAskedForMasternodeList.end())
	{
		if((*it1).second < GetTime())
		{
			mWeAskedForMasternodeList.erase(it1++);
		}
		else
		{
			++it1;
		}
	}

	// check which masternodes we've asked for
	std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.begin();
	while(it2 != mWeAskedForMasternodeListEntry.end())
	{
		if((*it2).second < GetTime())
		{
			mWeAskedForMasternodeListEntry.erase(it2++);
		}
		else
		{
			++it2;
		}
	}
}

void CMasternodeMan::Clear()
{
	LOCK(cs);

	vMasternodes.clear();
	mAskedUsForMasternodeList.clear();
	mWeAskedForMasternodeList.clear();
	mWeAskedForMasternodeListEntry.clear();

	nDsqCount = 0;
}

int CMasternodeMan::CountEnabled(int protocolVersion)
{
	int i = 0;
	protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

	for(CMasternode& mn : vMasternodes)
	{
		mn.Check();
		
		if(mn.protocolVersion < protocolVersion || !mn.IsEnabled())
		{
			continue;
		}
		
		i++;
	}

	return i;
}

// v2.0.0.8 voted-consensus determinism fix.
//
// Deterministic, chain-derived count of masternodes eligible to vote on
// nBlockHeight.  This is the consensus-denominator counterpart of
// CountEnabled() and must be used in its place by GetCanonicalWinner.
//
// Differences from CountEnabled() -- both deliberate:
//
//  1. Eligibility is CMasternode::IsVotingEligible(nBlockHeight), a pure
//     function of committed chain state (collateral confirmation depth), not
//     IsEnabled() which depends on wall-clock ping freshness.  Two nodes
//     calling this for the same nBlockHeight get the same answer; two nodes
//     calling CountEnabled() at different instants may not.  That divergence
//     was the root cause of the voted-consensus chain fork.
//
//  2. It does NOT call mn.Check().  Check() mutates masternode state (it can
//     flip activeState on a stale ping or spent collateral) and acquires
//     cs_main -- side effects that have no place in a consensus-denominator
//     read and that made CountEnabled() time-sensitive even beyond the
//     IsEnabled() value itself.  Spent-collateral MNs are removed from
//     vMasternodes by the normal CheckAndRemove lifecycle, so skipping
//     Check() here does not admit spent collateral into the count.
//
// The protocol-version floor (MIN_VOTING_PROTOCOL_VERSION via the caller) is
// retained: a peer too old to participate in the vote protocol must not
// inflate the denominator.  protocolVersion is itself fixed per MN, so this
// remains deterministic.
int CMasternodeMan::CountVotingEligible(int nBlockHeight, int protocolVersion)
{
	int i = 0;
	protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

	for(CMasternode& mn : vMasternodes)
	{
		if(mn.protocolVersion < protocolVersion)
		{
			continue;
		}

		if(!mn.IsVotingEligible(nBlockHeight))
		{
			continue;
		}

		i++;
	}

	return i;
}

int CMasternodeMan::CountMasternodesAboveProtocol(int protocolVersion)
{
	int i = 0;

	for(CMasternode& mn : vMasternodes)
	{
		mn.Check();
		
		if(mn.protocolVersion < protocolVersion || !mn.IsEnabled())
		{
			continue;
		}
		
		i++;
	}

	return i;
}

void CMasternodeMan::DsegUpdate(CNode* pnode)
{
	LOCK(cs);

	std::map<CNetAddr, int64_t>::iterator it = mWeAskedForMasternodeList.find(pnode->addr);

	if (it != mWeAskedForMasternodeList.end())
	{
		if (GetTime() < (*it).second)
		{
			LogPrintf("dseg - we already asked %s for the list; skipping...\n", pnode->addr.ToString());
			
			return;
		}
	}

	pnode->PushMessage("dseg", CTxIn());

	// v2.0.0.8 requester-side dseg retry.
	//
	// The original code always recorded askAgain = now + 3h after sending
	// a single dseg -- so if that one request was lost, the peer dropped
	// before replying, or the reply was partial, the node would not ask
	// again for THREE HOURS, starting cold and staying cold.
	//
	// Fix: choose the retry interval by whether the list is actually
	// populated.  size() == 0 means the previous dseg evidently did not
	// deliver -- retry on the short interval so the node recovers in
	// minutes.  Once the list is non-empty, use the full interval so a
	// node that synced cleanly does not re-ask peers unnecessarily.
	//
	// This is self-correcting and needs no extra state: each DsegUpdate
	// re-evaluates size() and sets the next interval accordingly.
	int64_t nRetryInterval = (this->size() == 0)
		? MASTERNODES_DSEG_RETRY_SECONDS
		: MASTERNODES_DSEG_SECONDS;

	int64_t askAgain = GetTime() + nRetryInterval;
	mWeAskedForMasternodeList[pnode->addr] = askAgain;
}

CMasternode *CMasternodeMan::Find(const CTxIn &vin)
{
	LOCK(cs);

	for(CMasternode& mn : vMasternodes)
	{
		if(mn.vin.prevout == vin.prevout)
		{
			return &mn;
		}
	}

	return NULL;
}

CMasternode* CMasternodeMan::FindOldestNotInVec(const std::vector<CTxIn> &vVins, int nMinimumAge)
{
	LOCK(cs);

	CMasternode *pOldestMasternode = NULL;

	for(CMasternode &mn : vMasternodes)
	{   
		mn.Check();
		
		if(!mn.IsEnabled())
		{
			continue;
		}
		
		if(mn.GetMasternodeInputAge() < nMinimumAge)
		{
			continue;
		}
		
		bool found = false;
		for(const CTxIn& vin : vVins)
		{
			if(mn.vin.prevout == vin.prevout)
			{   
				found = true;
				
				break;
			}
		}
		
		if(found)
		{
			continue;
		}
		
		if(pOldestMasternode == NULL || pOldestMasternode->SecondsSincePayment() < mn.SecondsSincePayment())
		{
			pOldestMasternode = &mn;
		}
	}

	return pOldestMasternode;
}

CMasternode *CMasternodeMan::FindRandom()
{
	LOCK(cs);

	if(size() == 0)
	{
		return NULL;
	}

	return &vMasternodes[GetRandInt(vMasternodes.size())];
}

CMasternode *CMasternodeMan::Find(const CPubKey &pubKeyMasternode)
{
	LOCK(cs);

	for(CMasternode& mn : vMasternodes)
	{
		if(mn.pubkey2 == pubKeyMasternode)
		{
			return &mn;
		}
	}

	return NULL;
}

CMasternode *CMasternodeMan::FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion)
{
	LOCK(cs);

	protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;
	int nCountEnabled = CountEnabled(protocolVersion);

	LogPrintf("CMasternodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());

	if(nCountEnabled - vecToExclude.size() < 1)
	{
		return NULL;
	}

	bool found;
	int rand = GetRandInt(nCountEnabled - vecToExclude.size());

	LogPrintf("CMasternodeMan::FindRandomNotInVec - rand %d\n", rand);


	for(CMasternode &mn : vMasternodes)
	{
		if(mn.protocolVersion < protocolVersion || !mn.IsEnabled())
		{
			continue;
		}
		
		found = false;
		
		for(CTxIn &usedVin : vecToExclude)
		{
			if(mn.vin.prevout == usedVin.prevout)
			{
				found = true;
				
				break;
			}
		}
		
		if(found)
		{
			continue;
		}
		
		if(--rand < 1)
		{
			return &mn;
		}
	}

	return NULL;
}

CMasternode* CMasternodeMan::GetCurrentMasterNode(int mod, int64_t nBlockHeight, int minProtocol)
{
	unsigned int score = 0;
	CMasternode* winner = NULL;

	// scan for winner
	for(CMasternode& mn : vMasternodes)
	{
		mn.Check();
		
		if(mn.protocolVersion < minProtocol || !mn.IsEnabled())
		{
			continue;
		}
		
		// calculate the score for each masternode
		uint256 n = mn.CalculateScore(mod, nBlockHeight);
		unsigned int n2 = 0;
		
		memcpy(&n2, &n, sizeof(n2));

		// determine the winner
		if(n2 > score)
		{
			score = n2;
			winner = &mn;
		}
	}

	return winner;
}

bool CMasternodeMan::IsPayeeAValidMasternode(CScript payee)
{
	if(!mnEnginePool.IsBlockchainSynced())
	{
		return true;
	}

	int mnCount = 0;
	bool fValid = false;

	for(CMasternode& mn : vMasternodes)
	{
		mn.Check();
		mnCount++;
		
		if(!mn.IsEnabled())
		{
			continue;
		}
		
		CScript currentMasternode = GetScriptForDestination(mn.pubkey.GetID());
		
		// LogPrintf("* Masternode %d - testing %s\n", mnCount, currentMasternode.ToString().c_str());
		
		if(payee == currentMasternode)
		{
		   fValid = true;
		}
	}

	return fValid;
}

std::vector<CMasternode> CMasternodeMan::GetFullMasternodeVector()
{
	this->Check();

	return vMasternodes;
}

int CMasternodeMan::GetMasternodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
	std::vector<std::pair<unsigned int, CTxIn>> vecMasternodeScores;

	//make sure we know about this block
	uint256 hash = 0;

	if(!GetBlockHash(hash, nBlockHeight))
	{
		return -1;
	}

	// scan for winner
	for(CMasternode& mn : vMasternodes)
	{
		if(mn.protocolVersion < minProtocol)
		{
			continue;
		}
		
		if(fOnlyActive)
		{
			mn.Check();
			
			if(!mn.IsEnabled())
			{
				continue;
			}
		}

		uint256 n = mn.CalculateScore(1, nBlockHeight);
		unsigned int n2 = 0;
		
		memcpy(&n2, &n, sizeof(n2));

		vecMasternodeScores.push_back(std::make_pair(n2, mn.vin));
	}

	sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareValueOnly<CTxIn>());

	int rank = 0;

	for(std::pair<unsigned int, CTxIn>& s : vecMasternodeScores)
	{
		rank++;
		
		if(s.second == vin)
		{
			return rank;
		}
	}

	return -1;
}

std::vector<std::pair<int, CMasternode>> CMasternodeMan::GetMasternodeRanks(int64_t nBlockHeight, int minProtocol)
{
	std::vector<std::pair<unsigned int, CMasternode>> vecMasternodeScores;
	std::vector<std::pair<int, CMasternode>> vecMasternodeRanks;

	//make sure we know about this block
	uint256 hash = 0;
	if(!GetBlockHash(hash, nBlockHeight))
	{
		return vecMasternodeRanks;
	}

	// scan for winner
	for(CMasternode& mn : vMasternodes)
	{
		mn.Check();

		if(mn.protocolVersion < minProtocol)
		{
			continue;
		}
		
		if(!mn.IsEnabled())
		{
			continue;
		}

		uint256 n = mn.CalculateScore(1, nBlockHeight);
		unsigned int n2 = 0;
		
		memcpy(&n2, &n, sizeof(n2));

		vecMasternodeScores.push_back(std::make_pair(n2, mn));
	}

	sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareValueOnly<CMasternode>());

	int rank = 0;
	for(std::pair<unsigned int, CMasternode>& s : vecMasternodeScores)
	{
		rank++;
		
		vecMasternodeRanks.push_back(std::make_pair(rank, s.second));
	}

	return vecMasternodeRanks;
}

CMasternode* CMasternodeMan::GetMasternodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
	std::vector<std::pair<unsigned int, CTxIn>> vecMasternodeScores;

	// scan for winner
	for(CMasternode& mn : vMasternodes)
	{
		if(mn.protocolVersion < minProtocol)
		{
			continue;
		}
		
		if(fOnlyActive)
		{
			mn.Check();
			
			if(!mn.IsEnabled())
			{
				continue;
			}
		}

		uint256 n = mn.CalculateScore(1, nBlockHeight);
		unsigned int n2 = 0;
		
		memcpy(&n2, &n, sizeof(n2));

		vecMasternodeScores.push_back(std::make_pair(n2, mn.vin));
	}

	sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareValueOnly<CTxIn>());

	int rank = 0;
	for(std::pair<unsigned int, CTxIn>& s : vecMasternodeScores)
	{
		rank++;
		
		if(rank == nRank)
		{
			return Find(s.second);
		}
	}

	return NULL;
}

void CMasternodeMan::ProcessMasternodeConnections()
{
	LOCK(cs_vNodes);

	if(!mnEnginePool.pSubmittedToMasternode)
	{
		return;
	}

	for(CNode* pnode : vNodes)
	{
		if(mnEnginePool.pSubmittedToMasternode->addr == pnode->addr)
		{
			continue;
		}
		
		if(pnode->fMNengineMaster)
		{
			LogPrintf("Closing masternode connection %s \n", pnode->addr.ToString().c_str());
			
			pnode->CloseSocketDisconnect();
		}
	}
}

void CMasternodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
	// this is a snapshot node. will only sync until certain block
	if (maxBlockHeight != -1 && pindexBest->nHeight >= maxBlockHeight)
	{
		return;
	}

	//Normally would disable functionality, NEED this enabled for staking.
	//if(fLiteMode)
	//{
	//	return;
	//}

	if(!mnEnginePool.IsBlockchainSynced())
	{
		return;
	}

	LOCK(cs_process_message);

	if (strCommand == "dsee") //MNengine Election Entry
	{
		CTxIn vin;
		CService addr;
		CPubKey pubkey;
		CPubKey pubkey2;
		std::vector<unsigned char> vchSig;
		int64_t sigTime;
		int count;
		int current;
		int64_t lastUpdated;
		int protocolVersion;
		CScript donationAddress;
		int donationPercentage;
		std::string strMessage;

		// 70047 and greater
		vRecv >> vin
				>> addr
				>> vchSig
				>> sigTime
				>> pubkey
				>> pubkey2
				>> count
				>> current
				>> lastUpdated
				>> protocolVersion
				>> donationAddress
				>> donationPercentage;

		// make sure signature isn't in the future (past is OK)
		if (sigTime > GetAdjustedTime() + 60 * 60)
		{
			LogPrintf("dsee - Signature rejected, too far into the future %s\n", vin.ToString().c_str());
			
			return;
		}

		bool isLocal = addr.IsRFC1918() || addr.IsLocal();
		//if(RegTest())
		//{
		//	isLocal = false;
		//}
		
		std::string vchPubKey(pubkey.begin(), pubkey.end());
		std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

		strMessage = addr.ToString() +
						boost::lexical_cast<std::string>(sigTime) +
						vchPubKey +
						vchPubKey2 +
						boost::lexical_cast<std::string>(protocolVersion) +
						donationAddress.ToString() +
						boost::lexical_cast<std::string>(donationPercentage);

		if(donationPercentage < 0 || donationPercentage > 100)
		{
			LogPrintf("dsee - donation percentage out of range %d\n", donationPercentage);
			
			return;     
		}
		
		if(protocolVersion < MIN_POOL_PEER_PROTO_VERSION)
		{
			LogPrintf("dsee - ignoring outdated masternode %s protocol version %d\n", vin.ToString().c_str(), protocolVersion);
			
			return;
		}

		CScript pubkeyScript;
		pubkeyScript.SetDestination(pubkey.GetID());

		if(pubkeyScript.size() != 25)
		{
			LogPrintf("dsee - pubkey the wrong size\n");
			
			Misbehaving(pfrom->GetId(), 100);
			
			return;
		}

		CScript pubkeyScript2;
		pubkeyScript2.SetDestination(pubkey2.GetID());

		if(pubkeyScript2.size() != 25)
		{
			LogPrintf("dsee - pubkey2 the wrong size\n");
			
			Misbehaving(pfrom->GetId(), 100);
			
			return;
		}

		if(!vin.scriptSig.empty())
		{
			LogPrintf("dsee - Ignore Not Empty ScriptSig %s\n",vin.ToString().c_str());
			
			return;
		}

		std::string errorMessage = "";
		
		if(!mnEngineSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage))
		{
			LogPrintf("dsee - WARNING - Could not verify masternode address signature\n");
			
			Misbehaving(pfrom->GetId(), 100);
			
			return;
		}

		//search existing masternode list, this is where we update existing masternodes with new dsee broadcasts
		CMasternode* pmn = this->Find(vin);
		
		// if we are a masternode but with undefined vin and this dsee is ours (matches our Masternode privkey) then just skip this part
		if(pmn != NULL && !(fMasterNode && activeMasternode.vin == CTxIn() && pubkey2 == activeMasternode.pubKeyMasternode))
		{
			// Detect whether the incoming dsee carries field changes compared
			// to what we have cached.  If yes, we MUST process it -- even if
			// dseep heartbeats have kept lastTimeSeen fresh -- so that
			// protocol/addr/donation changes propagate network-wide.  Prior
			// to this fix the `!UpdatedWithin(5min)` gate silently dropped
			// virtually all live update broadcasts because dseep heartbeats
			// (arriving every minute) kept lastTimeSeen too recent for the
			// gate to ever open.  See UAT-followup.md U2.
			bool fieldsChanged = (pmn->protocolVersion != protocolVersion ||
								  pmn->addr != addr ||
								  pmn->donationAddress != donationAddress ||
								  pmn->donationPercentage != donationPercentage ||
								  pmn->pubkey2 != pubkey2);

			// Anti-replay is the primary defense: only accept newer sigTime.
			// Anti-spoof: pubkey must match what we have cached.
			// Anti-flood: rate-limit IDENTICAL dsees (no field change) via
			// the original UpdatedWithin check; bypass that limit when
			// actual change is being reported.
			bool acceptable =
				(pmn->pubkey == pubkey) &&
				(pmn->sigTime < sigTime) &&
				(count == -1 || fieldsChanged) &&
				(!pmn->UpdatedWithin(MASTERNODE_MIN_DSEE_SECONDS) || fieldsChanged);

			if(acceptable)
			{
				pmn->UpdateLastSeen();

				// v2.0.0.8 hotfix Issue 1: wire Path A auto-clear into the
				// dsee known-MN-update path.  Without this call the documented
				// "Cleared by OnFreshDsee (Path A)" contract is dead code in
				// steady state -- the existing call site at line ~1036 only
				// fires in the new-MN-add path.  See
				// v208-Issue1-OnFreshDsee-wiring-SPEC.md.
				voteTracker.OnFreshDsee(vin.prevout);

				if (!CheckNode((CAddress)addr))
				{
					pmn->isPortOpen = false;
				}
				else
				{
					pmn->isPortOpen = true;
					addrman.Add(CAddress(addr), pfrom->addr, 2*60*60); // use this as a peer
				}

				LogPrintf("dsee - Got updated entry for %s%s\n",
						  addr.ToString().c_str(),
						  fieldsChanged ? " (fields changed)" : "");

				pmn->pubkey2 = pubkey2;
				pmn->sigTime = sigTime;
				pmn->sig = vchSig;
				pmn->protocolVersion = protocolVersion;
				pmn->addr = addr;
				pmn->donationAddress = donationAddress;
				pmn->donationPercentage = donationPercentage;
				pmn->Check();

				// v2.0.0.8 M3 patch 4: if this MN has a stale (paidHeight==0)
				// cache entry, take this opportunity to walk the chain and
				// fix it.  Handles the case where an observer restarts after
				// MNs joined the network -- old mncache.dat lacks payments
				// for those MNs, this dsee path now backfills.  See U3.
				if (mapLastPaidHeight.find(vin.prevout) == mapLastPaidHeight.end())
				{
					RecomputeLastPaidHeight(pmn);
				}

				// Only RELAY when this was a live broadcast (count == -1).
				// Sync-time responses (count != -1) are accepted for local
				// state update but not amplified -- the originating peer is
				// already broadcasting the live version network-wide.
				if(count == -1 && pmn->IsEnabled())
				{
					mnodeman.RelayMasternodeEntry(
						vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current,
						lastUpdated, protocolVersion, donationAddress, donationPercentage
					);
				}
			}

			return;
		}

		// make sure the vout that was signed is related to the transaction that spawned the masternode
		//  - this is expensive, so it's only done once per masternode
		if(!mnEngineSigner.IsVinAssociatedWithPubkey(vin, pubkey))
		{
			LogPrintf("dsee - Got mismatched pubkey and vin\n");
			
			Misbehaving(pfrom->GetId(), 100);
			
			return;
		}

		LogPrint("masternode", "dsee - Got NEW masternode entry %s\n", addr.ToString().c_str());

		// make sure it's still unspent
		//  - this is checked later by .check() in many places and by ThreadCheckMNenginePool()

		CValidationState state;
		CTransaction tx = CTransaction();
		CTxOut vout = CTxOut(MNengine_POOL_MAX, mnEnginePool.collateralPubKey);
		tx.vin.push_back(vin);
		tx.vout.push_back(vout);
		bool fAcceptable = false;
		
		{
			TRY_LOCK(cs_main, lockMain);
			
			if(!lockMain)
			{
				return;
			}
			
			fAcceptable = AcceptableInputs(mempool, tx, false, NULL);
		}
		
		if(fAcceptable)
		{
			LogPrint("masternode", "dsee - Accepted masternode entry %i %i\n", count, current);

			if(GetInputAge(vin) < MASTERNODE_MIN_CONFIRMATIONS)
			{
				LogPrintf("dsee - Input must have least %d confirmations\n", MASTERNODE_MIN_CONFIRMATIONS);
				
				// v2.0.0.8 Round 4-A: GetInputAge returns the collateral
				// confirmation depth RELATIVE TO THIS NODE'S OWN CHAIN
				// HEIGHT.  A node that is still syncing / behind sees a
				// lower age than a fully-synced node, so a perfectly
				// valid dsee for a real masternode can fail this check
				// purely because the receiver has not caught up.  Only
				// score misbehaviour when NOT in initial block download
				// -- i.e. when a too-young collateral is a real protocol
				// fault rather than a local sync-gap artifact.  The dsee
				// is rejected (return) either way.
				if (!IsInitialBlockDownload())
				{
					Misbehaving(pfrom->GetId(), 20);
				}
				
				return;
			}

			// verify that sig time is legit in past
			// should be at least not earlier than block when 2,000,000 DigitalNote tx got MASTERNODE_MIN_CONFIRMATIONS
			uint256 hashBlock = 0;
			GetTransaction(vin.prevout.hash, tx, hashBlock);
			std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
			
			if (mi != mapBlockIndex.end() && (*mi).second)
			{
				CBlockIndex* pMNIndex = (*mi).second; // block for 2,000,000 DigitalNote tx -> 1 confirmation
				CBlockIndex* pConfIndex = FindBlockByHeight((pMNIndex->nHeight + MASTERNODE_MIN_CONFIRMATIONS - 1)); // block where tx got MASTERNODE_MIN_CONFIRMATIONS
				
				if(pConfIndex->GetBlockTime() > sigTime)
				{
					LogPrintf(
						"dsee - Bad sigTime %d for masternode %20s %105s (%i conf block is at %d)\n",
						sigTime,
						addr.ToString(),
						vin.ToString(),
						MASTERNODE_MIN_CONFIRMATIONS,
						pConfIndex->GetBlockTime()
					);
					
					return;
				}
			}
			
			// use this as a peer
			addrman.Add(CAddress(addr), pfrom->addr, 2*60*60);

			//doesn't support multisig addresses
			if(donationAddress.IsPayToScriptHash())
			{
				donationAddress = CScript();
				donationPercentage = 0;
			}

			// add our masternode
			CMasternode mn(addr, vin, pubkey, vchSig, sigTime, pubkey2, protocolVersion, donationAddress, donationPercentage);
			mn.UpdateLastSeen(lastUpdated);
			this->Add(mn);

			// v2.0.0.8 M3: Path A equivocation recovery.  A fresh dsee from
			// this voter clears their equivocator status (if under the
			// MAX_EQUIVOCATIONS_PER_SESSION cap).  Path B is the
			// clearequivocator RPC.
			voteTracker.OnFreshDsee(vin.prevout);

			// if it matches our masternodeprivkey, then we've been remotely activated
			if(pubkey2 == activeMasternode.pubKeyMasternode && protocolVersion >= MIN_PEER_PROTO_VERSION)
			{
				activeMasternode.EnableHotColdMasterNode(vin, addr);
			}

			if(count == -1 && !isLocal)
			{
				mnodeman.RelayMasternodeEntry(
					vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current,
					lastUpdated, protocolVersion, donationAddress, donationPercentage
				);
			}
		}
		else
		{
			LogPrintf("dsee - Rejected masternode entry %s\n", addr.ToString().c_str());

			int nDoS = 0;
			
			if (state.IsInvalid(nDoS))
			{
				LogPrintf(
					"dsee - %s from %s %s was not accepted into the memory pool\n",
					tx.GetHash().ToString().c_str(),
					pfrom->addr.ToString().c_str(),
					pfrom->cleanSubVer.c_str()
				);
				
				if (nDoS > 0)
				{
					Misbehaving(pfrom->GetId(), nDoS);
				}
			}
		}
	}
	else if (strCommand == "dseep") //MNengine Election Entry Ping
	{
		CTxIn vin;
		std::vector<unsigned char> vchSig;
		int64_t sigTime;
		bool stop;
		
		vRecv >> vin >> vchSig >> sigTime >> stop;

		//LogPrintf("dseep - Received: vin: %s sigTime: %lld stop: %s\n", vin.ToString().c_str(), sigTime, stop ? "true" : "false");

		if (sigTime > GetAdjustedTime() + 60 * 60)
		{
			LogPrintf("dseep - Signature rejected, too far into the future %s\n", vin.ToString().c_str());
			
			return;
		}

		if (sigTime <= GetAdjustedTime() - 60 * 60)
		{
			LogPrintf("dseep - Signature rejected, too far into the past %s - %d %d \n", vin.ToString().c_str(), sigTime, GetAdjustedTime());
			
			return;
		}

		// see if we have this masternode
		CMasternode* pmn = this->Find(vin);
		
		if(pmn != NULL && pmn->protocolVersion >= MIN_POOL_PEER_PROTO_VERSION)
		{
			// LogPrintf("dseep - Found corresponding mn for vin: %s\n", vin.ToString().c_str());
			// take this only if it's newer
			if(pmn->lastDseep < sigTime)
			{
				std::string strMessage = pmn->addr.ToString() + boost::lexical_cast<std::string>(sigTime) + boost::lexical_cast<std::string>(stop);
				std::string errorMessage = "";
				
				if(!mnEngineSigner.VerifyMessage(pmn->pubkey2, vchSig, strMessage, errorMessage))
				{
					LogPrintf("dseep - WARNING - Could not verify masternode address signature %s \n", vin.ToString().c_str());
					
					//Misbehaving(pfrom->GetId(), 100);
					
					return;
				}

				pmn->lastDseep = sigTime;

				if(!pmn->UpdatedWithin(MASTERNODE_MIN_DSEEP_SECONDS))
				{
					if(stop)
					{
						pmn->Disable();
					}
					else
					{
						pmn->UpdateLastSeen();

						// v2.0.0.8 hotfix Issue 1: wire Path A auto-clear into
						// the dseep heartbeat handler.  dseep is the frequent
						// (~MASTERNODE_MIN_DSEEP_SECONDS) heartbeat; without
						// this call equivocator state can only auto-clear via
						// dsee broadcasts, which are rare in steady state.
						// See v208-Issue1-OnFreshDsee-wiring-SPEC.md.
						voteTracker.OnFreshDsee(vin.prevout);

						pmn->Check();
						
						if(!pmn->IsEnabled())
						{
							return;
						}
					}
					
					mnodeman.RelayMasternodeEntryPing(vin, vchSig, sigTime, stop);
				}
			}
			
			return;
		}

		LogPrint("masternode", "dseep - Couldn't find masternode entry %s\n", vin.ToString().c_str());

		std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasternodeListEntry.find(vin.prevout);
		
		if (i != mWeAskedForMasternodeListEntry.end())
		{
			int64_t t = (*i).second;
			
			if (GetTime() < t)
			{
				return; // we've asked recently
			}
		}

		// ask for the dsee info once from the node that sent dseep

		LogPrintf("dseep - Asking source node for missing entry %s\n", vin.ToString().c_str());
		
		pfrom->PushMessage("dseg", vin);
		
		int64_t askAgain = GetTime()+ MASTERNODE_MIN_DSEEP_SECONDS;
		
		mWeAskedForMasternodeListEntry[vin.prevout] = askAgain;
	}
	else if (strCommand == "mvote") //Masternode Vote
	{
		CTxIn vin;
		std::vector<unsigned char> vchSig;
		int nVote;
		
		vRecv >> vin >> vchSig >> nVote;

		// see if we have this Masternode
		CMasternode* pmn = this->Find(vin);
		
		if(pmn != NULL)
		{
			if((GetAdjustedTime() - pmn->lastVote) > (60*60))
			{
				std::string strMessage = vin.ToString() + boost::lexical_cast<std::string>(nVote);
				std::string errorMessage = "";
				
				if(!mnEngineSigner.VerifyMessage(pmn->pubkey2, vchSig, strMessage, errorMessage))
				{
					LogPrintf("mvote - WARNING - Could not verify masternode address signature %s \n", vin.ToString().c_str());
					
					return;
				}

				pmn->nVote = nVote;
				pmn->lastVote = GetAdjustedTime();

				//send to all peers
				LOCK(cs_vNodes);
				
				for(CNode* pnode : vNodes)
				{
					pnode->PushMessage("mvote", vin, vchSig, nVote);
				}
			}

			return;
		}
	}
	else if (strCommand == "dseg") //Get masternode list or specific entry
	{
		CTxIn vin;
		vRecv >> vin;

		if(vin == CTxIn()) //only should ask for this once
		{
			//local network
			if(!pfrom->addr.IsRFC1918() && Params().NetworkID() == CChainParams_Network::MAIN)
			{
				std::map<CNetAddr, int64_t>::iterator i = mAskedUsForMasternodeList.find(pfrom->addr);
				
				if (i != mAskedUsForMasternodeList.end())
				{
					int64_t t = (*i).second;
					
					if (GetTime() < t)
					{
						// v2.0.0.8: a peer re-asking for the masternode
						// list inside the rate-limit window is rate-limited
						// and ignored -- it is NOT scored as misbehaviour.
						// The original code applied Misbehaving(pfrom, 34),
						// a third of a ban, for a re-ask.  A peer that
						// restarts, reconnects, or legitimately re-syncs
						// after a dropped connection has every reason to
						// ask again; punishing that toward a ban is wrong
						// and harms list propagation.  Just drop the
						// duplicate request.
						LogPrintf("dseg - peer %s asked for the list again too soon; ignoring\n",
								  pfrom->addr.ToString());
						
						return;
					}
				}

				int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
				mAskedUsForMasternodeList[pfrom->addr] = askAgain;
			}
		} //else, asking for a specific node which is ok

		int count = this->size();
		int i = 0;

		for(CMasternode& mn : vMasternodes)
		{
			if(mn.addr.IsRFC1918())
			{
				continue; //local network
			}
			
			if(mn.IsEnabled())
			{
				LogPrint("masternode", "dseg - Sending masternode entry - %s \n", mn.addr.ToString().c_str());
				
				if(vin == CTxIn())
				{
					pfrom->PushMessage("dsee", mn.vin, mn.addr, mn.sig, mn.sigTime, mn.pubkey, mn.pubkey2, count, i, mn.lastTimeSeen, mn.protocolVersion, mn.donationAddress, mn.donationPercentage);
				}
				else if (vin == mn.vin)
				{
					pfrom->PushMessage("dsee", mn.vin, mn.addr, mn.sig, mn.sigTime, mn.pubkey, mn.pubkey2, count, i, mn.lastTimeSeen, mn.protocolVersion, mn.donationAddress, mn.donationPercentage);
					
					LogPrintf("dseg - Sent 1 masternode entries to %s\n", pfrom->addr.ToString().c_str());
					
					return;
				}
				
				i++;
			}
		}

		LogPrintf("dseg - Sent %d masternode entries to %s\n", i, pfrom->addr.ToString().c_str());
	}
}

void CMasternodeMan::RelayMasternodeEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey, const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion, CScript donationAddress, int donationPercentage)
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		pnode->PushMessage(
			"dsee", vin, addr, vchSig, nNow, pubkey, pubkey2, count, current,
			lastUpdated, protocolVersion, donationAddress, donationPercentage
		);
	}
}

void CMasternodeMan::RelayMasternodeEntryPing(const CTxIn vin, const std::vector<unsigned char> vchSig, const int64_t nNow, const bool stop)
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		pnode->PushMessage("dseep", vin, vchSig, nNow, stop);
	}
}

void CMasternodeMan::Remove(CTxIn vin)
{
	LOCK(cs);

	for(std::vector<CMasternode>::iterator it = vMasternodes.begin(); it != vMasternodes.end(); it++)
	{
		if((*it).vin == vin)
		{
			LogPrint("masternode", "CMasternodeMan: Removing Masternode %s - %i now\n", (*it).addr.ToString().c_str(), size() - 1);

			vMasternodes.erase(it);

			break;
		}
	}
}

std::string CMasternodeMan::ToString() const
{
	std::ostringstream info;

	info << "masternodes: " << (int)vMasternodes.size() <<
			", peers who asked us for masternode list: " << (int)mAskedUsForMasternodeList.size() <<
			", peers we asked for masternode list: " << (int)mWeAskedForMasternodeList.size() <<
			", entries in Masternode list we asked for: " << (int)mWeAskedForMasternodeListEntry.size() <<
			", nDsqCount: " << (int)nDsqCount;

	return info.str();
}

/*
	Return the number of (unique) masternodes
*/    
int CMasternodeMan::size()
{
	return vMasternodes.size();
}

// ===========================================================================
// v2.0.0.8 M1: chain-derived last-paid-height cache
// ===========================================================================

CMasternode* CMasternodeMan::FindByPayeeAddress(const CTxDestination& dest)
{
	LOCK(cs);

	// Convert the destination to a script for comparison.  This bypasses the
	// boost::variant<>::operator== requirement that some destination leaf
	// types (notably CStealthAddress) don't fully satisfy.  Pattern matches
	// existing IsPayeeAValidMasternode.
	CScript scriptForDest = GetScriptForDestination(dest);

	for (CMasternode& mn : vMasternodes)
	{
		CScript mnScript = GetScriptForDestination(mn.pubkey.GetID());

		if (mnScript == scriptForDest)
		{
			return &mn;
		}
	}

	return NULL;
}

int CMasternodeMan::GetLastPaidHeight(const COutPoint& vinPrevout) const
{
	LOCK(cs);

	std::map<COutPoint, int>::const_iterator it = mapLastPaidHeight.find(vinPrevout);

	if (it == mapLastPaidHeight.end())
	{
		return 0;
	}

	return it->second;
}

std::vector<CMnPaymentSnapshotEntry> CMasternodeMan::GetQueuePaymentSnapshot() const
{
	// v2.0.0.8 M1Q spec S18.1: capture every MN's chain-derived payment
	// state under a SINGLE cs acquisition, so the queue simulation runs
	// against a coherent, immutable snapshot and never re-enters the lock.
	LOCK(cs);

	std::vector<CMnPaymentSnapshotEntry> result;
	result.reserve(vMasternodes.size());

	for (std::vector<CMasternode>::const_iterator it = vMasternodes.begin();
		 it != vMasternodes.end(); ++it)
	{
		const CMasternode &mn = *it;

		CMnPaymentSnapshotEntry e;
		e.vin = mn.vin.prevout;
		e.payeeScript = GetScriptForDestination(mn.pubkey.GetID());
		e.confirmedHeight = mn.GetCollateralConfirmedHeight();

		std::map<COutPoint, int>::const_iterator pit = mapLastPaidHeight.find(mn.vin.prevout);
		if (pit != mapLastPaidHeight.end())
		{
			e.hasPaid = true;
			e.paidHeight = pit->second;
		}
		else
		{
			e.hasPaid = false;
			e.paidHeight = 0;
		}

		result.push_back(e);
	}

	return result;
}

void CMasternodeMan::OnBlockConnected(const CBlock& block, int nBlockHeight)
{
	// Pick the payment-bearing transaction.  PoS: coinstake (vtx[1]).
	// PoW: coinbase (vtx[0]).  Verified against production blocks per
	// PhaseA-current-state.md S4.2.
	const CTransaction* paymentTx = NULL;

	if (block.IsProofOfStake() && block.vtx.size() >= 2)
	{
		paymentTx = &block.vtx[1];
	}
	else if (block.vtx.size() >= 1)
	{
		paymentTx = &block.vtx[0];
	}
	else
	{
		return;
	}

	// Scan outputs, find the one paying a known MN.  Address-based
	// identification handles PoS and PoW uniformly, regardless of vout
	// position (which varies with stake-split / devops presence).
	for (const CTxOut& out : paymentTx->vout)
	{
		if (out.nValue == 0)
		{
			continue;
		}

		CTxDestination dest;

		if (!ExtractDestination(out.scriptPubKey, dest))
		{
			continue;
		}

		CMasternode* mn = FindByPayeeAddress(dest);

		if (mn != NULL)
		{
			{
				LOCK(cs);
				mapLastPaidHeight[mn->vin.prevout] = nBlockHeight;
			}

			// Also update the MN's nLastPaid for display purposes only;
			// selection no longer relies on this field.
			mn->nLastPaid = block.GetBlockTime();

			LogPrint("masternode", "CMasternodeMan::OnBlockConnected -- MN %s paid at height %d\n",
					  mn->vin.prevout.ToString(), nBlockHeight);

			return;  // only one MN payment per block expected
		}
	}
}

void CMasternodeMan::OnBlockDisconnected(const CBlock& block, int nBlockHeight)
{
	const CTransaction* paymentTx = NULL;

	if (block.IsProofOfStake() && block.vtx.size() >= 2)
	{
		paymentTx = &block.vtx[1];
	}
	else if (block.vtx.size() >= 1)
	{
		paymentTx = &block.vtx[0];
	}
	else
	{
		return;
	}

	for (const CTxOut& out : paymentTx->vout)
	{
		if (out.nValue == 0)
		{
			continue;
		}

		CTxDestination dest;

		if (!ExtractDestination(out.scriptPubKey, dest))
		{
			continue;
		}

		CMasternode* mn = FindByPayeeAddress(dest);

		if (mn != NULL)
		{
			// Only recompute if this block is the cached last-paid block for
			// this MN.  Disconnecting an earlier payment block has no effect
			// on the most-recent record.
			bool needsRecompute = false;

			{
				LOCK(cs);

				std::map<COutPoint, int>::iterator it = mapLastPaidHeight.find(mn->vin.prevout);

				if (it != mapLastPaidHeight.end() && it->second == nBlockHeight)
				{
					needsRecompute = true;
				}
			}

			if (needsRecompute)
			{
				LogPrintf("CMasternodeMan::OnBlockDisconnected -- recomputing lastPaidHeight "
						  "for MN %s (was %d)\n",
						  mn->vin.prevout.ToString(), nBlockHeight);

				RecomputeLastPaidHeight(mn);
			}

			return;
		}
	}
}

void CMasternodeMan::RecomputeLastPaidHeight(CMasternode* mn)
{
	if (mn == NULL || pindexBest == NULL)
	{
		return;
	}

	CScript mnScript = GetScriptForDestination(mn->pubkey.GetID());

	CBlockIndex* pindex = pindexBest;

	// v2.0.0.8 PB-6: bound the walk by MAX_LASTPAID_SCAN_DEPTH, NOT by
	// nLastPaidHeightScannedTo.
	//
	// The previous bound was `pindex->nHeight > nLastPaidHeightScannedTo`.
	// nLastPaidHeightScannedTo is set to wherever the startup scan
	// (PopulateLastPaidHeightCache) TERMINATED -- and that scan
	// terminates EARLY, as soon as a payment has been found for every
	// enabled MN.  On a healthy chain that is only tens of blocks below
	// the tip.  So the old bound made this function scan only a tiny
	// recent window.
	//
	// Consequences of the old bound:
	//   - A masternode that joins via dsee AFTER startup, whose last
	//     payment is deeper than that shallow window, was never found.
	//     RecomputeLastPaidHeight then fell through to the erase() below
	//     and the MN was wrongly treated as never-paid (longest-ago) --
	//     so it was over-prioritised and won votes it should not have.
	//   - On a reorg (OnBlockDisconnected path) that disconnects an MN's
	//     cached last-paid block, the recompute likewise only looked at
	//     the shallow window and could erase a still-valid older entry.
	//
	// The comment at the Add() call site claimed the scannedTo bound
	// kept the walk "small" because scannedTo was "a recent point" --
	// that reasoning was inverted: a recent floor makes the walk small
	// precisely BY missing most of the chain.
	//
	// Fix: walk up to MAX_LASTPAID_SCAN_DEPTH blocks, exactly like
	// PopulateLastPaidHeightCache does.  A late-joining MN now gets the
	// same quality of answer the startup scan gives.  The walk still
	// stops the instant the MN's payment is found, so for any genuinely
	// active MN the cost is only ~(fleet size) block reads; the full
	// depth is reached only for an MN with no payment in the last
	// MAX_LASTPAID_SCAN_DEPTH blocks, for which "erase / treat as
	// longest-ago" is the correct answer anyway.  Add() is cold (MNs
	// join minutes apart), so the deeper bound is not a hot-path cost.
	int nWalked = 0;

	while (pindex != NULL && nWalked < MAX_LASTPAID_SCAN_DEPTH)
	{
		CBlock block;

		if (!block.ReadFromDisk(pindex))
		{
			pindex = pindex->pprev;
			nWalked++;
			continue;
		}

		const CTransaction* paymentTx = NULL;

		if (block.IsProofOfStake() && block.vtx.size() >= 2)
		{
			paymentTx = &block.vtx[1];
		}
		else if (block.vtx.size() >= 1)
		{
			paymentTx = &block.vtx[0];
		}

		if (paymentTx != NULL)
		{
			for (const CTxOut& out : paymentTx->vout)
			{
				if (out.nValue == 0)
				{
					continue;
				}

				CTxDestination dest;

				if (!ExtractDestination(out.scriptPubKey, dest))
				{
					continue;
				}

				CScript outScript = GetScriptForDestination(dest);

				if (outScript == mnScript)
				{
					{
						LOCK(cs);
						mapLastPaidHeight[mn->vin.prevout] = pindex->nHeight;
					}

					LogPrintf("CMasternodeMan::RecomputeLastPaidHeight -- found MN %s at height %d "
							  "(walked %d blocks)\n",
							  mn->vin.prevout.ToString(), pindex->nHeight, nWalked);

					return;
				}
			}
		}

		pindex = pindex->pprev;
		nWalked++;
	}

	// Not found within MAX_LASTPAID_SCAN_DEPTH blocks.  Remove entry so
	// MN is treated as "never paid in our window" (longest-ago-paid for
	// selection).  With the depth-bounded walk this now genuinely means
	// "no payment in the last MAX_LASTPAID_SCAN_DEPTH blocks", which is
	// the correct condition for that treatment.
	{
		LOCK(cs);
		mapLastPaidHeight.erase(mn->vin.prevout);
	}

	LogPrintf("CMasternodeMan::RecomputeLastPaidHeight -- MN %s not found within %d blocks\n",
			  mn->vin.prevout.ToString(), MAX_LASTPAID_SCAN_DEPTH);
}

void CMasternodeMan::PopulateLastPaidHeightCache()
{
	if (pindexBest == NULL)
	{
		LogPrintf("CMasternodeMan::PopulateLastPaidHeightCache -- no chain loaded yet, skipping\n");
		return;
	}

	int64_t nStartTime = GetTimeMillis();
	int nStartHeight = pindexBest->nHeight;
	int nWalked = 0;
	int nFound = 0;

	// Snapshot of MN identities we still need to find a payment for.
	// Keyed by COutPoint; CScript is the payee script we'll match against
	// block outputs.  Storing scripts directly avoids re-deriving them on
	// every output we look at.
	std::map<COutPoint, CScript> stillNeeded;

	{
		LOCK(cs);

		for (CMasternode& mn : vMasternodes)
		{
			// Only populate for enabled MNs.  Disabled MNs that have never
			// been paid will get entries lazily on their next dsee + payment.
			if (!mn.IsEnabled())
			{
				continue;
			}

			stillNeeded[mn.vin.prevout] = GetScriptForDestination(mn.pubkey.GetID());
		}
	}

	if (stillNeeded.empty())
	{
		LogPrintf("CMasternodeMan::PopulateLastPaidHeightCache -- no enabled MNs to scan for\n");

		LOCK(cs);
		nLastPaidHeightScannedTo = nStartHeight;
		return;
	}

	LogPrintf("CMasternodeMan::PopulateLastPaidHeightCache -- scanning back from height %d "
			  "for %u enabled MNs (max depth %d blocks)\n",
			  nStartHeight, (unsigned)stillNeeded.size(), MAX_LASTPAID_SCAN_DEPTH);

	// v2.0.0.8 UAT-4: max blocks we MIGHT walk, used for the progress
	// display.  The actual walk usually ends early (all MNs found long
	// before this cap), but we don't know how many in advance.
	int nProgressDenom = MAX_LASTPAID_SCAN_DEPTH;

	// v2.0.0.8 UAT-4: emit an initial splash message so the user sees
	// activity even before the first 100-block tick.  The Qt splash's
	// transparent region goes black if no paint events arrive for a few
	// hundred ms; the periodic InitMessage in the loop body keeps the
	// splash repainting throughout the walk.
	uiInterface.InitMessage(strprintf("MN cache: 0/%d", nProgressDenom));

	CBlockIndex* pindex = pindexBest;

	while (pindex != NULL && nWalked < MAX_LASTPAID_SCAN_DEPTH && !stillNeeded.empty())
	{
		// v2.0.0.8 UAT-4: throttled progress emit.  Every 100 blocks is
		// frequent enough to keep the splash alive (Qt collapses redundant
		// repaints) and infrequent enough that the InitMessage calls
		// themselves don't add measurable overhead to the walk.
		if ((nWalked % 100) == 0 && nWalked > 0)
		{
			uiInterface.InitMessage(strprintf("MN cache: %d/%d", nWalked, nProgressDenom));
		}

		CBlock block;

		if (!block.ReadFromDisk(pindex))
		{
			pindex = pindex->pprev;
			nWalked++;
			continue;
		}

		const CTransaction* paymentTx = NULL;

		if (block.IsProofOfStake() && block.vtx.size() >= 2)
		{
			paymentTx = &block.vtx[1];
		}
		else if (block.vtx.size() >= 1)
		{
			paymentTx = &block.vtx[0];
		}

		if (paymentTx != NULL)
		{
			// Build script set for this block's outputs once, then check each
			// still-needed MN against it.
			for (const CTxOut& out : paymentTx->vout)
			{
				if (out.nValue == 0)
				{
					continue;
				}

				CTxDestination dest;

				if (!ExtractDestination(out.scriptPubKey, dest))
				{
					continue;
				}

				CScript outScript = GetScriptForDestination(dest);

				// Linear scan of stillNeeded -- with ~30 MNs this is trivial.
				std::map<COutPoint, CScript>::iterator matchIt = stillNeeded.end();

				for (std::map<COutPoint, CScript>::iterator it = stillNeeded.begin();
					 it != stillNeeded.end(); ++it)
				{
					if (it->second == outScript)
					{
						matchIt = it;
						break;
					}
				}

				if (matchIt != stillNeeded.end())
				{
					{
						LOCK(cs);
						mapLastPaidHeight[matchIt->first] = pindex->nHeight;
					}

					stillNeeded.erase(matchIt);
					nFound++;
					break;  // each block has at most one MN payment
				}
			}
		}

		pindex = pindex->pprev;
		nWalked++;
	}

	{
		LOCK(cs);
		nLastPaidHeightScannedTo = (pindex == NULL) ? 0 : pindex->nHeight;
	}

	int64_t nElapsedMs = GetTimeMillis() - nStartTime;

	LogPrintf("CMasternodeMan::PopulateLastPaidHeightCache -- done.  Walked %d blocks, "
			  "found %d MNs, %u still needed, elapsed %dms\n",
			  nWalked, nFound,
			  (unsigned)stillNeeded.size(),
			  (int)nElapsedMs);

	if (!stillNeeded.empty())
	{
		LogPrintf("CMasternodeMan::PopulateLastPaidHeightCache -- %u MNs not seen in scanned "
				  "range; treated as longest-ago-paid\n",
				  (unsigned)stillNeeded.size());
	}
}

CMasternode* CMasternodeMan::FindOldestNotInVecChainDerived(const std::vector<CTxIn>& vVins,
															int nMinimumAge,
															int nReferenceHeight,
															bool fChainDerivedEligibility)
{
	// v2.0.0.8 PB-INFLIGHT REVERTED.
	//
	// PB-INFLIGHT (added 2026-05-21) folded voteTracker.GetConsensusCommittedHeights()
	// into the paidHeight comparison below, intending to stop an MN being
	// re-nominated for successive heights in the VOTE_LOOKAHEAD window
	// before its voted block connected.  It has been removed in full --
	// the fetch here and the fold in the loop -- for two reasons:
	//
	// 1. It was a fix for a non-problem.  The "payee streaks under slow
	//    blocks" PB-INFLIGHT targeted were an artefact of the 2026-05-21
	//    testnet, which at that time had a duplicate-masternode-identity
	//    fault (two daemons equivocating as one vin) corrupting the vote
	//    data the diagnosis rested on.  On a correctly-configured fleet,
	//    BroadcastVote logs show flawless rotation through block gaps of
	//    8-11 minutes (testnet heights 827-888, 2026-05-23/24): the
	//    candidate function rotates cleanly on mapLastPaidHeight alone.
	//    The cache being ~VOTE_LOOKAHEAD behind the voted height is not
	//    a bug -- it is simply the lookahead, and it is harmless.
	//
	// 2. It was itself a consensus-correctness bug.  GetConsensusCommittedHeights
	//    iterates the vote tracker's mapVotes -- node-local, in-flight
	//    tally state that legitimately differs between nodes that have
	//    received votes in a different order or at a different time.
	//    Folding it into paidHeight made the vote a node-local function
	//    sees diverge: geographically separated masternode clusters
	//    computed different winners from byte-identical mapLastPaidHeight
	//    caches (confirmed testnet heights 880/883, 2026-05-24 -- a
	//    stable 5/2 split along the network boundary).  A consensus
	//    input must be a pure function of the chain, never of node-local
	//    tally state -- the same principle the Fix C / IsVotingEligible
	//    work in this file already enforces.
	//
	// With PB-INFLIGHT removed -- and, as of Spec B, the PB-16
	// activation clamp removed too -- this function is a pure function
	// of (vMasternodes, mapLastPaidHeight, per-MN collateral confirm
	// heights) -- all chain-derived and identical on every synced node.
	// GetConsensusCommittedHeights has been removed from the vote
	// tracker as it now has no caller.

	LOCK(cs);

	CMasternode* pOldestMasternode = NULL;
	int nOldestPaidHeight = INT_MAX;
	COutPoint outBestTiebreak;

	// v2.0.0.8 Spec B: the PB-16 pre-activation lastpaid clamp has been
	// REMOVED.  PB-16 normalised every pre-activation lastpaid value to
	// activationHeight - 1, intending to neutralise arbitrary legacy
	// values.  But collapsing multiple MNs to one identical paidHeight
	// made them TIE, and the smallest-vin tiebreak then froze selection
	// on one MN until it was paid (~VOTE_LOOKAHEAD blocks later) -- a
	// payee streak.  Proven on testnet: a PoS stall straddling the
	// activation height left several MNs last-paid below activation at
	// once; on resume they all clamped to the same value and the chain
	// produced clean period-10 payee streaks (heights ~1596-1757),
	// cleared only by a restart (which rebuilds mapLastPaidHeight from
	// the real chain, all-distinct).
	//
	// The clamp is not needed.  mapLastPaidHeight stores block HEIGHTS,
	// which do not go stale with wall-clock time -- only with block
	// progression -- so last-paid ORDER is correct in every epoch with
	// no normalisation.  The arbitrary-legacy-value concern PB-16 cited
	// self-heals: the genuinely longest-ago-paid MN wins, is paid, and
	// within one rotation cycle (~fleet size) the legacy spread is
	// flushed -- harmless, no streak.
	//
	// Never-paid MNs (no mapLastPaidHeight entry) are handled below by
	// the collateral confirmation height, NOT by paidHeight 0 -- see the
	// lookup block.  This keeps the selector a pure function of
	// (vMasternodes, mapLastPaidHeight, collateral confirm heights) --
	// all chain-derived, identical on every synced node.

	for (CMasternode& mn : vMasternodes)
	{
		mn.Check();

		// v2.0.0.8 Fix C: candidate-pool eligibility predicate.
		//
		// Vote path (fChainDerivedEligibility == true): use the
		// deterministic, chain-derived IsVotingEligible(nReferenceHeight).
		// Every node computing a vote for the same height then sees the
		// SAME candidate pool, so FindOldestNotInVecChainDerived returns
		// the same MN -- a precondition for the vote bucket to agree on a
		// payee and reach consensus.  IsEnabled() must NOT be used here:
		// it depends on wall-clock ping freshness and differs node to
		// node, so it would reintroduce per-node payee divergence.
		//
		// Legacy path (fChainDerivedEligibility == false, the default):
		// keep the original IsEnabled() liveness filter unchanged.
		if (fChainDerivedEligibility)
		{
			if (!mn.IsVotingEligible(nReferenceHeight))
			{
				continue;
			}
		}
		else
		{
			if (!mn.IsEnabled())
			{
				continue;
			}
		}

		if (mn.GetMasternodeInputAge() < nMinimumAge)
		{
			continue;
		}

		bool found = false;
		for (const CTxIn& vin : vVins)
		{
			if (mn.vin.prevout == vin.prevout)
			{
				found = true;
				break;
			}
		}

		if (found)
		{
			continue;
		}

		// Chain-derived last-paid lookup (v2.0.0.8 Spec B).
		// - Cache entry present: use the real last-paid height as-is.
		//   This includes entries above nReferenceHeight (the reorg-risk
		//   zone); OnBlockDisconnected rolls the cache back on reorg, so
		//   votes from an about-to-be-orphaned segment self-correct.
		// - No cache entry: the MN has never been paid in the scanned
		//   range.  Rank it by its COLLATERAL CONFIRMATION HEIGHT, not by
		//   0.  Rationale: a never-paid MN's fair queue position is "when
		//   it joined the chain".  Using 0 would make every never-paid MN
		//   tie at 0 and let the smallest-vin tiebreak freeze on one of
		//   them -- the very tie-collapse bug PB-16 caused.  Confirmation
		//   height is unique per MN, chain-derived, identical on every
		//   node, and orders newcomers correctly behind earlier joiners
		//   and ahead of nobody unfairly.  A flapping MN that loses its
		//   cache entry keeps its original confirm height, so rejoining
		//   does not jump the queue.
		//
		// If the collateral cannot be resolved on this node
		// (GetCollateralConfirmedHeight() < 0) the MN would already have
		// failed IsVotingEligible above on the vote path and been
		// skipped; on the legacy path treat it as paidHeight 0 (oldest)
		// -- it cannot poison consensus because the legacy path does not
		// feed enforcement.
		int paidHeight;
		std::map<COutPoint, int>::const_iterator it = mapLastPaidHeight.find(mn.vin.prevout);

		if (it != mapLastPaidHeight.end())
		{
			paidHeight = it->second;
		}
		else
		{
			int nConfirmed = mn.GetCollateralConfirmedHeight();
			paidHeight = (nConfirmed >= 0) ? nConfirmed : 0;
		}

		// NB: the PB-16 pre-activation clamp that previously sat here has
		// been removed -- see the function-head comment.  No clamp: the
		// real (or confirm-height-derived) value is used directly.

		// Pick the MN with the smallest paidHeight (longest-ago paid).
		// Tie-break on lowest vin.prevout for determinism + grind-resistance.
		bool better = false;

		if (pOldestMasternode == NULL)
		{
			better = true;
		}
		else if (paidHeight < nOldestPaidHeight)
		{
			better = true;
		}
		else if (paidHeight == nOldestPaidHeight && mn.vin.prevout < outBestTiebreak)
		{
			better = true;
		}

		if (better)
		{
			pOldestMasternode = &mn;
			nOldestPaidHeight = paidHeight;
			outBestTiebreak = mn.vin.prevout;
		}
	}

	return pOldestMasternode;
}

// ===========================================================================

unsigned int CMasternodeMan::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;

	s.nType = nType;
	s.nVersion = nVersion;
	
	// serialized format:
	// * version byte (currently 0)
	// * masternodes vector
	{
		LOCK(cs);

		unsigned char nVersion = 0;
		READWRITE(nVersion);
		READWRITE(vMasternodes);
		READWRITE(mAskedUsForMasternodeList);
		READWRITE(mWeAskedForMasternodeList);
		READWRITE(mWeAskedForMasternodeListEntry);
		READWRITE(nDsqCount);
	}
	
	return nSerSize;
}

template<typename Stream>
void CMasternodeMan::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	// serialized format:
	// * version byte (currently 0)
	// * masternodes vector
	{
		LOCK(cs);

		unsigned char nVersion = 0;
		READWRITE(nVersion);
		READWRITE(vMasternodes);
		READWRITE(mAskedUsForMasternodeList);
		READWRITE(mWeAskedForMasternodeList);
		READWRITE(mWeAskedForMasternodeListEntry);
		READWRITE(nDsqCount);
	}
}

template<typename Stream>
void CMasternodeMan::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	// serialized format:
	// * version byte (currently 0)
	// * masternodes vector
	{
		LOCK(cs);

		unsigned char nVersion = 0;
		READWRITE(nVersion);
		READWRITE(vMasternodes);
		READWRITE(mAskedUsForMasternodeList);
		READWRITE(mWeAskedForMasternodeList);
		READWRITE(mWeAskedForMasternodeListEntry);
		READWRITE(nDsqCount);
	}
}

template void CMasternodeMan::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CMasternodeMan::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

