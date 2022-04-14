#include "compat.h"

#include <boost/lexical_cast.hpp>

#include "main.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "mining.h"
#include "caddrman.h"
#include "cblockindex.h"
#include "cvalidationstate.h"
#include "net.h"
#include "net/cnode.h"
#include "util.h"
#include "serialize.h"
#include "cmasternode.h"
#include "cmasternodepayments.h"
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

	int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
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
		
		LogPrintf("* Masternode %d - testing %s\n", mnCount, currentMasternode.ToString().c_str());
		
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
			// count == -1 when it's a new entry
			//   e.g. We don't want the entry relayed/time updated when we're syncing the list
			// mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
			//   after that they just need to match
			if(count == -1 && pmn->pubkey == pubkey && !pmn->UpdatedWithin(MASTERNODE_MIN_DSEE_SECONDS))
			{
				pmn->UpdateLastSeen();

				if(pmn->sigTime < sigTime) //take the newest entry
				{
					if (!CheckNode((CAddress)addr))
					{
						pmn->isPortOpen = false;
					}
					else
					{
						pmn->isPortOpen = true;
						addrman.Add(CAddress(addr), pfrom->addr, 2*60*60); // use this as a peer
					}
					
					LogPrintf("dsee - Got updated entry for %s\n", addr.ToString().c_str());
					
					pmn->pubkey2 = pubkey2;
					pmn->sigTime = sigTime;
					pmn->sig = vchSig;
					pmn->protocolVersion = protocolVersion;
					pmn->addr = addr;
					pmn->donationAddress = donationAddress;
					pmn->donationPercentage = donationPercentage;
					pmn->Check();
					
					if(pmn->IsEnabled())
					{
						mnodeman.RelayMasternodeEntry(
							vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current,
							lastUpdated, protocolVersion, donationAddress, donationPercentage
						);
					}
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
				
				Misbehaving(pfrom->GetId(), 20);
				
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

			// if it matches our masternodeprivkey, then we've been remotely activated
			if(pubkey2 == activeMasternode.pubKeyMasternode && protocolVersion == PROTOCOL_VERSION)
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
						Misbehaving(pfrom->GetId(), 34);
						
						LogPrintf("dseg - peer already asked me for the list\n");
						
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

unsigned int CMasternodeMan::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	const bool fGetSize = true;
	const bool fWrite = false;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
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
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
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
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
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

