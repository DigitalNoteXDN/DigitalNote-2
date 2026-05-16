#include "compat.h"

#include "cmasternodevotetracker.h"

#include "util.h"
#include "thread.h"
#include "ctxin.h"
#include "coutpoint.h"
#include "net.h"
#include "net/cnode.h"
#include "cdatastream.h"
#include "main.h"
#include "main_extern.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "masternode_extern.h"
#include "masternode.h"
#include "cblockindex.h"
#include "cinv.h"

/*
 * v2.0.0.8 M3 vote tracker.
 *
 * State model (PhaseC-design.md S9):
 *   mapVotes[height][payee]: VoteRecord of voters who voted for this payee.
 *     Updated by ProcessVote.  Pruned by OnBlockConnected when height drops
 *     below (currentTip - VOTE_PAST_HORIZON).
 *
 *   setSeenVoter: per-(height, voterVin) dedup.  ProcessVote checks before
 *     adding to mapVotes; if already present, the vote is a duplicate (drop)
 *     or an equivocation (handle).
 *
 *   mapEquivocationDetection[voterVin] = (lastHeight, lastPayee): tracks the
 *     last (height, payee) we saw from each voter.  When a NEW vote comes in
 *     for the SAME height but a DIFFERENT payee, we have equivocation.
 *
 *   mapEquivocators[voterVin]: voters whose votes we currently reject.
 *     Cleared by OnFreshDsee (Path A) or ClearEquivocator RPC (Path B).
 *
 *   mapVotesByHash: vote.GetHash() -> full vote, for inv-based relay support
 *     (AlreadyHave + getdata).  Pruned in lockstep with mapVotes.
 *
 * Threading: every public method acquires cs.  Internal helpers assume cs is
 * already held; documented in their per-method comments.  No external locks
 * are acquired while holding cs to avoid lock-order issues.
 */

CMasternodeVoteTracker voteTracker;

CMasternodeVoteTracker::CMasternodeVoteTracker()
{
}

// ---------------------------------------------------------------------------
// Internal helpers (assume cs is held by caller)
// ---------------------------------------------------------------------------

static std::pair<int, COutPoint> MakeVoterKey(int nBlockHeight, const COutPoint &voterVin)
{
	return std::make_pair(nBlockHeight, voterVin);
}

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

bool CMasternodeVoteTracker::ProcessVote(const CMasternodeVote &vote, CNode *pfrom)
{
	if (pindexBest == NULL)
	{
		return false;
	}

	int currentTip = pindexBest->nHeight;

	if (vote.nBlockHeight > currentTip + VOTE_LOOKAHEAD + REORG_DEPTH_BUFFER)
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::ProcessVote -- reject: vote height %d "
					  "exceeds tip %d + lookahead+buffer\n",
					  vote.nBlockHeight, currentTip);
		}
		return false;
	}

	if (vote.nBlockHeight < currentTip - VOTE_PAST_HORIZON)
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::ProcessVote -- reject: vote height %d "
					  "below tip %d - past horizon %d\n",
					  vote.nBlockHeight, currentTip, VOTE_PAST_HORIZON);
		}
		return false;
	}

	int64_t now = GetAdjustedTime();
	if (vote.nTimeSigned > now + VOTE_TIME_WINDOW_SECONDS)
	{
		LogPrintf("CMasternodeVoteTracker::ProcessVote -- reject: vote nTimeSigned %d "
				  "is %d seconds in the future\n",
				  (int)vote.nTimeSigned, (int)(vote.nTimeSigned - now));
		return false;
	}
	if (vote.nTimeSigned < now - VOTE_TIME_WINDOW_SECONDS)
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::ProcessVote -- reject: vote nTimeSigned %d "
					  "is %d seconds in the past\n",
					  (int)vote.nTimeSigned, (int)(now - vote.nTimeSigned));
		}
		return false;
	}

	LOCK(cs);

	COutPoint voterOutpoint = vote.voterVin.prevout;

	if (mapEquivocators.count(voterOutpoint))
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::ProcessVote -- reject: voter %s "
					  "is equivocator (count %d)\n",
					  voterOutpoint.ToString(),
					  mapEquivocators[voterOutpoint].count);
		}
		return false;
	}

	std::pair<int, COutPoint> voterKey = MakeVoterKey(vote.nBlockHeight, voterOutpoint);

	if (setSeenVoter.count(voterKey))
	{
		std::map<COutPoint, std::pair<int, CScript> >::iterator detIt =
				mapEquivocationDetection.find(voterOutpoint);

		if (detIt != mapEquivocationDetection.end() &&
			detIt->second.first == vote.nBlockHeight &&
			detIt->second.second != vote.payeeScript)
		{
			LogPrintf("CMasternodeVoteTracker::ProcessVote -- EQUIVOCATION detected: "
					  "voter %s at height %d voted for two different payees\n",
					  voterOutpoint.ToString(), vote.nBlockHeight);

			RemoveVoterVote(vote.nBlockHeight, voterOutpoint);

			EquivocationRecord &rec = mapEquivocators[voterOutpoint];
			rec.count++;
			rec.lastEquivocationTime = now;

			return false;
		}

		return false;
	}

	setSeenVoter.insert(voterKey);
	mapEquivocationDetection[voterOutpoint] = std::make_pair(vote.nBlockHeight, vote.payeeScript);

	VoteRecord &record = mapVotes[vote.nBlockHeight][vote.payeeScript];

	if (record.voterVins.empty())
	{
		record.nBlockHeight = vote.nBlockHeight;
		record.payeeScript = vote.payeeScript;
		record.nFirstSeen = now;
	}

	record.voterVins.insert(voterOutpoint);

	mapVotesByHash[vote.GetHash()] = vote;

	if (fDebug)
	{
		LogPrintf("CMasternodeVoteTracker::ProcessVote -- recorded vote from %s "
				  "for height %d (%u voters now agree on this payee)\n",
				  voterOutpoint.ToString(),
				  vote.nBlockHeight,
				  (unsigned)record.voterVins.size());
	}

	(void)pfrom;
	return true;
}

bool CMasternodeVoteTracker::GetCanonicalWinner(int nBlockHeight, CScript &payeeOut)
{
	LOCK(cs);

	std::map<int, std::map<CScript, VoteRecord> >::iterator heightIt = mapVotes.find(nBlockHeight);
	if (heightIt == mapVotes.end())
	{
		return false;
	}

	int eligibleVoters = mnodeman.CountEnabled(MIN_VOTING_PROTOCOL_VERSION);

	if (eligibleVoters < MIN_ENABLED_FOR_CONSENSUS)
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::GetCanonicalWinner -- below floor: "
					  "only %d eligible voters (< %d)\n",
					  eligibleVoters, MIN_ENABLED_FOR_CONSENSUS);
		}
		return false;
	}

	const std::map<CScript, VoteRecord> &payees = heightIt->second;

	for (std::map<CScript, VoteRecord>::const_iterator pit = payees.begin();
		 pit != payees.end(); ++pit)
	{
		int voteCount = (int)pit->second.voterVins.size();

		if ((int64_t)voteCount * VOTED_CONSENSUS_THRESHOLD_DENOMINATOR >=
			(int64_t)eligibleVoters * VOTED_CONSENSUS_THRESHOLD_NUMERATOR)
		{
			payeeOut = pit->first;

			if (fDebug)
			{
				LogPrintf("CMasternodeVoteTracker::GetCanonicalWinner -- height %d: "
						  "consensus reached (%d/%d voters, threshold %d/%d)\n",
						  nBlockHeight, voteCount, eligibleVoters,
						  VOTED_CONSENSUS_THRESHOLD_NUMERATOR,
						  VOTED_CONSENSUS_THRESHOLD_DENOMINATOR);
			}

			return true;
		}
	}

	return false;
}

void CMasternodeVoteTracker::OnBlockConnected(int nBlockHeight)
{
	LOCK(cs);

	int pruneBelow = nBlockHeight - VOTE_PAST_HORIZON;

	for (std::map<int, std::map<CScript, VoteRecord> >::iterator it = mapVotes.begin();
		 it != mapVotes.end(); )
	{
		if (it->first < pruneBelow)
		{
			int heightToPrune = it->first;

			const std::map<CScript, VoteRecord> &payees = it->second;
			for (std::map<CScript, VoteRecord>::const_iterator pit = payees.begin();
				 pit != payees.end(); ++pit)
			{
				for (std::set<COutPoint>::const_iterator vit = pit->second.voterVins.begin();
					 vit != pit->second.voterVins.end(); ++vit)
				{
					setSeenVoter.erase(MakeVoterKey(heightToPrune, *vit));
				}
			}

			for (std::map<COutPoint, std::pair<int, CScript> >::iterator dit =
					mapEquivocationDetection.begin();
				 dit != mapEquivocationDetection.end(); )
			{
				if (dit->second.first == heightToPrune)
				{
					mapEquivocationDetection.erase(dit++);
				}
				else
				{
					++dit;
				}
			}

			mapVotes.erase(it++);
		}
		else
		{
			++it;
		}
	}

	for (std::map<uint256, CMasternodeVote>::iterator it = mapVotesByHash.begin();
		 it != mapVotesByHash.end(); )
	{
		if (it->second.nBlockHeight < pruneBelow)
		{
			mapVotesByHash.erase(it++);
		}
		else
		{
			++it;
		}
	}
}

void CMasternodeVoteTracker::OnBlockDisconnected(int nBlockHeight)
{
	LOCK(cs);

	for (std::map<COutPoint, std::pair<int, CScript> >::iterator dit =
			mapEquivocationDetection.begin();
		 dit != mapEquivocationDetection.end(); )
	{
		if (dit->second.first == nBlockHeight)
		{
			mapEquivocationDetection.erase(dit++);
		}
		else
		{
			++dit;
		}
	}

	for (std::set<std::pair<int, COutPoint> >::iterator it = setSeenVoter.begin();
		 it != setSeenVoter.end(); )
	{
		if (it->first == nBlockHeight)
		{
			setSeenVoter.erase(it++);
		}
		else
		{
			++it;
		}
	}
}

void CMasternodeVoteTracker::OnFreshDsee(const COutPoint &voterVin)
{
	LOCK(cs);

	std::map<COutPoint, EquivocationRecord>::iterator it = mapEquivocators.find(voterVin);
	if (it == mapEquivocators.end())
	{
		return;
	}

	if (it->second.count >= MAX_EQUIVOCATIONS_PER_SESSION)
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::OnFreshDsee -- voter %s exceeded %d "
					  "equivocations; ignoring fresh dsee (Path A disabled)\n",
					  voterVin.ToString(), MAX_EQUIVOCATIONS_PER_SESSION);
		}
		return;
	}

	mapEquivocators.erase(it);
	mapEquivocationDetection.erase(voterVin);

	LogPrintf("CMasternodeVoteTracker::OnFreshDsee -- cleared equivocator status for %s "
			  "(Path A: fresh dsee)\n",
			  voterVin.ToString());
}

bool CMasternodeVoteTracker::ClearEquivocator(const COutPoint &voterVin)
{
	LOCK(cs);

	std::map<COutPoint, EquivocationRecord>::iterator it = mapEquivocators.find(voterVin);
	if (it == mapEquivocators.end())
	{
		return false;
	}

	mapEquivocators.erase(it);
	mapEquivocationDetection.erase(voterVin);

	LogPrintf("CMasternodeVoteTracker::ClearEquivocator -- cleared equivocator status for %s "
			  "(Path B: operator RPC)\n",
			  voterVin.ToString());

	return true;
}

bool CMasternodeVoteTracker::IsEquivocator(const COutPoint &voterVin) const
{
	LOCK(cs);

	return mapEquivocators.count(voterVin) > 0;
}

void CMasternodeVoteTracker::RemoveVoterVote(int nBlockHeight, const COutPoint &voterVin)
{
	// Assumes cs is held by caller.

	setSeenVoter.erase(MakeVoterKey(nBlockHeight, voterVin));

	std::map<int, std::map<CScript, VoteRecord> >::iterator heightIt = mapVotes.find(nBlockHeight);
	if (heightIt == mapVotes.end())
	{
		return;
	}

	std::map<CScript, VoteRecord> &payees = heightIt->second;
	for (std::map<CScript, VoteRecord>::iterator pit = payees.begin();
		 pit != payees.end(); ++pit)
	{
		pit->second.voterVins.erase(voterVin);
	}
}

// ---------------------------------------------------------------------------
// M3 inv-based relay support
// ---------------------------------------------------------------------------

bool CMasternodeVoteTracker::AlreadyHaveVote(const uint256 &hash) const
{
	LOCK(cs);

	return mapVotesByHash.count(hash) > 0;
}

bool CMasternodeVoteTracker::GetVoteByHash(const uint256 &hash, CMasternodeVote &voteOut) const
{
	LOCK(cs);

	std::map<uint256, CMasternodeVote>::const_iterator it = mapVotesByHash.find(hash);
	if (it == mapVotesByHash.end())
	{
		return false;
	}

	voteOut = it->second;
	return true;
}

void CMasternodeVoteTracker::Sync(CNode *pnode)
{
	if (pnode == NULL)
	{
		return;
	}

	std::vector<CInv> vInv;

	{
		LOCK(cs);

		vInv.reserve(mapVotesByHash.size());

		for (std::map<uint256, CMasternodeVote>::const_iterator it = mapVotesByHash.begin();
			 it != mapVotesByHash.end(); ++it)
		{
			vInv.push_back(CInv(MSG_MASTERNODE_VOTE, it->first));
		}
	}

	if (!vInv.empty())
	{
		pnode->PushMessage("inv", vInv);

		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::Sync -- pushed %u vote invs to peer %d\n",
					  (unsigned)vInv.size(), pnode->GetId());
		}
	}
}

// ---------------------------------------------------------------------------
// M3 RPC support helpers
// ---------------------------------------------------------------------------

std::vector<CMasternodeVoteTracker::EquivocatorInfo>
CMasternodeVoteTracker::GetEquivocatorList() const
{
	LOCK(cs);

	std::vector<EquivocatorInfo> result;
	result.reserve(mapEquivocators.size());

	for (std::map<COutPoint, EquivocationRecord>::const_iterator it = mapEquivocators.begin();
		 it != mapEquivocators.end(); ++it)
	{
		EquivocatorInfo info;
		info.voterVin = it->first;
		info.count = it->second.count;
		info.lastEquivocationTime = it->second.lastEquivocationTime;
		info.autoClearingAvailable = (it->second.count < MAX_EQUIVOCATIONS_PER_SESSION);
		result.push_back(info);
	}

	return result;
}

CMasternodeVoteTracker::VoteInfo
CMasternodeVoteTracker::GetVoteInfo(int nBlockHeight) const
{
	LOCK(cs);

	VoteInfo info;
	info.height = nBlockHeight;
	info.totalVotes = 0;
	info.eligibleVoters = mnodeman.CountEnabled(MIN_VOTING_PROTOCOL_VERSION);
	info.hasConsensus = false;
	info.canonicalVoteCount = 0;

	std::map<int, std::map<CScript, VoteRecord> >::const_iterator heightIt =
			mapVotes.find(nBlockHeight);
	if (heightIt == mapVotes.end())
	{
		return info;
	}

	const std::map<CScript, VoteRecord> &payees = heightIt->second;

	int bestCount = 0;
	CScript bestPayee;

	for (std::map<CScript, VoteRecord>::const_iterator pit = payees.begin();
		 pit != payees.end(); ++pit)
	{
		VoteInfoEntry entry;
		entry.payeeScript = pit->first;
		entry.voterVins = pit->second.voterVins;
		entry.firstSeen = pit->second.nFirstSeen;

		info.perPayee.push_back(entry);

		int n = (int)pit->second.voterVins.size();
		info.totalVotes += n;

		if (n > bestCount)
		{
			bestCount = n;
			bestPayee = pit->first;
		}
	}

	if (info.eligibleVoters >= MIN_ENABLED_FOR_CONSENSUS &&
		(int64_t)bestCount * VOTED_CONSENSUS_THRESHOLD_DENOMINATOR >=
		(int64_t)info.eligibleVoters * VOTED_CONSENSUS_THRESHOLD_NUMERATOR)
	{
		info.hasConsensus = true;
		info.canonicalPayee = bestPayee;
		info.canonicalVoteCount = bestCount;
	}

	return info;
}

std::map<COutPoint, int>
CMasternodeVoteTracker::GetVoterActivity() const
{
	LOCK(cs);

	// Walk every (height, payee, voter) entry, recording the max height per
	// voter.  Single pass; total work is sum of all voter sets across mapVotes.
	// At ~30 MNs * ~20 active heights this is trivial (~600 entries max).
	std::map<COutPoint, int> result;

	for (std::map<int, std::map<CScript, VoteRecord> >::const_iterator hit = mapVotes.begin();
		 hit != mapVotes.end(); ++hit)
	{
		int height = hit->first;

		for (std::map<CScript, VoteRecord>::const_iterator pit = hit->second.begin();
			 pit != hit->second.end(); ++pit)
		{
			for (std::set<COutPoint>::const_iterator vit = pit->second.voterVins.begin();
				 vit != pit->second.voterVins.end(); ++vit)
			{
				// Insert or update with max-height-seen.
				std::map<COutPoint, int>::iterator existing = result.find(*vit);

				if (existing == result.end())
				{
					result[*vit] = height;
				}
				else if (height > existing->second)
				{
					existing->second = height;
				}
			}
		}
	}

	return result;
}

// ---------------------------------------------------------------------------
// M3: message dispatcher (refactored from M2 -- uses real tracker state now)
// ---------------------------------------------------------------------------

void ProcessMessageMasternodeVote(CNode *pfrom, std::string &strCommand, CDataStream &vRecv)
{
	if (fLiteMode)
	{
		return;
	}

	if (strCommand == "mnvote")
	{
		CMasternodeVote vote;

		try
		{
			vRecv >> vote;
		}
		catch (std::exception &e)
		{
			LogPrintf("mnvote -- failed to deserialize from peer %d: %s\n",
					  pfrom ? pfrom->GetId() : -1, e.what());
			if (pfrom)
			{
				Misbehaving(pfrom->GetId(), 100);
			}
			return;
		}

		if (pindexBest == NULL)
		{
			return;
		}

		CMasternode *voter = mnodeman.Find(vote.voterVin);
		if (voter == NULL)
		{
			if (fDebug)
			{
				LogPrintf("mnvote -- unknown voter %s at height %d, asking for dsee\n",
						  vote.voterVin.prevout.ToString(), vote.nBlockHeight);
			}
			if (pfrom)
			{
				mnodeman.AskForMN(pfrom, vote.voterVin);
			}
			return;
		}

		if (!vote.CheckSignature(voter->pubkey2))
		{
			LogPrintf("mnvote -- invalid signature from %s height %d (peer %d)\n",
					  vote.voterVin.prevout.ToString(), vote.nBlockHeight,
					  pfrom ? pfrom->GetId() : -1);
			if (pfrom)
			{
				Misbehaving(pfrom->GetId(), 100);
			}
			return;
		}

		// Early-out for known votes (avoids the heavier tally path).
		if (voteTracker.AlreadyHaveVote(vote.GetHash()))
		{
			return;
		}

		bool added = voteTracker.ProcessVote(vote, pfrom);

		if (!added)
		{
			return;
		}

		LogPrintf("mnvote -- accepted vote from %s for height %d (peer %d)\n",
				  vote.voterVin.prevout.ToString(), vote.nBlockHeight,
				  pfrom ? pfrom->GetId() : -1);

		CInv inv(MSG_MASTERNODE_VOTE, vote.GetHash());
		std::vector<CInv> vInv;
		vInv.push_back(inv);

		{
			LOCK(cs_vNodes);

			for (CNode *pnode : vNodes)
			{
				if (pnode == pfrom)
				{
					continue;
				}
				pnode->PushMessage("inv", vInv);
			}
		}

		return;
	}

	if (strCommand == "getmnvotes")
	{
		voteTracker.Sync(pfrom);
		return;
	}
}
