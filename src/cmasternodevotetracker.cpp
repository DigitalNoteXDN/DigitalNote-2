#include "compat.h"

#include "cmasternodevotetracker.h"
#include "cmasternodevotequeue.h"

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
 * v2.0.0.8 M1Q queue-based voting tracker.
 *
 * State model (v208-M1Q-queue-based-voting-SPEC.md):
 *   mapQueues[nQueueHeight][voterVin]: CMasternodeVoteQueue.  One queue
 *     per voter per nQueueHeight.  Storage of all in-flight queues.
 *     Pruned by OnBlockConnectedQueues when nQueueHeight falls below
 *     (currentTip - VOTE_PAST_HORIZON).
 *
 *   mapQueuesByHash: queue.GetHash() -> full queue, for inv-based relay
 *     (AlreadyHaveQueue + getdata).  Pruned in lockstep with mapQueues.
 *
 *   mapEquivocationDetection[voterVin] = (nQueueHeight, queueHash):
 *     last queue we saw from each voter.  A NEW queue from the same
 *     voter at the same nQueueHeight with a different hash is
 *     equivocation.
 *
 *   mapEquivocators[voterVin]: voters whose queues we currently reject.
 *     Cleared by OnFreshDsee (Path A) or ClearEquivocator RPC (Path B).
 *
 * Threading: every public method acquires cs.  Internal helpers assume
 * cs is already held; documented in their per-method comments.
 * GetCanonicalWinnerFromQueues and GetQueueInfo take LOCK2(cs_main, cs)
 * because they descend into CountVotingEligible -> GetTransaction
 * (cs_main) and would otherwise violate canonical lock order (see CW5).
 */

CMasternodeVoteTracker voteTracker;

CMasternodeVoteTracker::CMasternodeVoteTracker()
{
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------





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



// ===========================================================================
// v2.0.0.8 M1Q -- queue-based voting method definitions.
//
// Storage shape: mapQueues[nQueueHeight][voterVin] -> the voter's queue.
// One queue per voter per nQueueHeight.  See
// v208-M1Q-queue-based-voting-SPEC.md.
// ===========================================================================

bool CMasternodeVoteTracker::AlreadyHaveQueue(const uint256 &hash) const
{
	LOCK(cs);

	return mapQueuesByHash.count(hash) > 0;
}

bool CMasternodeVoteTracker::GetQueueByHash(const uint256 &hash,
											CMasternodeVoteQueue &queueOut) const
{
	LOCK(cs);

	std::map<uint256, CMasternodeVoteQueue>::const_iterator it = mapQueuesByHash.find(hash);
	if (it == mapQueuesByHash.end())
	{
		return false;
	}

	queueOut = it->second;
	return true;
}

void CMasternodeVoteTracker::SyncQueues(CNode *pnode)
{
	if (pnode == NULL)
	{
		return;
	}

	std::vector<CInv> vInv;

	{
		LOCK(cs);

		vInv.reserve(mapQueuesByHash.size());

		for (std::map<uint256, CMasternodeVoteQueue>::const_iterator it = mapQueuesByHash.begin();
			 it != mapQueuesByHash.end(); ++it)
		{
			vInv.push_back(CInv(MSG_MASTERNODE_VOTE_QUEUE, it->first));
		}
	}

	if (!vInv.empty())
	{
		pnode->PushMessage("inv", vInv);

		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::SyncQueues -- pushed %u queue invs to peer %d\n",
					  (unsigned)vInv.size(), pnode->GetId());
		}
	}
}

bool CMasternodeVoteTracker::ProcessQueue(const CMasternodeVoteQueue &q, CNode *pfrom)
{
	if (pindexBest == NULL)
	{
		return false;
	}

	int currentTip = pindexBest->nHeight;

	// Window check on nQueueHeight (defense in depth; the message handler
	// also checks, but ProcessQueue must be safe if called from elsewhere).
	if (q.nQueueHeight > currentTip + REORG_DEPTH_BUFFER)
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::ProcessQueue -- reject: nQueueHeight %d "
					  "exceeds tip %d + reorg buffer\n",
					  q.nQueueHeight, currentTip);
		}
		return false;
	}

	if (q.nQueueHeight < currentTip - VOTE_PAST_HORIZON)
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::ProcessQueue -- reject: nQueueHeight %d "
					  "below tip %d - past horizon %d\n",
					  q.nQueueHeight, currentTip, VOTE_PAST_HORIZON);
		}
		return false;
	}

	// Time-window check (mirrors ProcessVote).
	int64_t now = GetAdjustedTime();
	if (q.nTimeSigned > now + VOTE_TIME_WINDOW_SECONDS)
	{
		LogPrintf("CMasternodeVoteTracker::ProcessQueue -- reject: nTimeSigned %d "
				  "is %d seconds in the future\n",
				  (int)q.nTimeSigned, (int)(q.nTimeSigned - now));
		return false;
	}
	if (q.nTimeSigned < now - VOTE_TIME_WINDOW_SECONDS)
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::ProcessQueue -- reject: nTimeSigned %d "
					  "is %d seconds in the past\n",
					  (int)q.nTimeSigned, (int)(now - q.nTimeSigned));
		}
		return false;
	}

	// Queue-length bound (defense in depth; the message handler also checks).
	if ((int)q.vPayeeQueue.size() != VOTE_QUEUE_LENGTH)
	{
		LogPrintf("CMasternodeVoteTracker::ProcessQueue -- reject: queue size %d "
				  "!= VOTE_QUEUE_LENGTH %d\n",
				  (int)q.vPayeeQueue.size(), VOTE_QUEUE_LENGTH);
		return false;
	}

	LOCK(cs);

	COutPoint voterOutpoint = q.voterVin.prevout;

	// Equivocator gate -- a voter previously marked equivocator is refused
	// (persists across the wire-format change; same mapEquivocators as the
	// per-height vote path, keyed on voterVin -- M1Q decision Q-C).
	if (mapEquivocators.count(voterOutpoint))
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::ProcessQueue -- reject: voter %s "
					  "is equivocator (count %d)\n",
					  voterOutpoint.ToString(),
					  mapEquivocators[voterOutpoint].count);
		}
		return false;
	}

	// Equivocation detection (M1Q spec S8): a queue is uniquely identified
	// by (voterVin, nQueueHeight).  If we already hold a queue from this
	// voter at this nQueueHeight:
	//   * identical hash    -> duplicate, drop silently (no error).
	//   * different hash     -> EQUIVOCATION (two distinct queues for the
	//                           same identity), record and reject.
	std::map<int, std::map<COutPoint, CMasternodeVoteQueue> >::iterator qhIt =
			mapQueues.find(q.nQueueHeight);

	if (qhIt != mapQueues.end())
	{
		std::map<COutPoint, CMasternodeVoteQueue>::iterator existing =
				qhIt->second.find(voterOutpoint);

		if (existing != qhIt->second.end())
		{
			if (existing->second.GetHash() == q.GetHash())
			{
				// Exact duplicate -- already stored, nothing to do.
				return false;
			}

			// Distinct queue for the same (voter, nQueueHeight) -- equivocation.
			LogPrintf("CMasternodeVoteTracker::ProcessQueue -- EQUIVOCATION detected: "
					  "voter %s at nQueueHeight %d sent two distinct queues\n",
					  voterOutpoint.ToString(), q.nQueueHeight);

			// Remove the prior queue (and its by-hash entry) and record the
			// equivocator.  The new queue is rejected.
			mapQueuesByHash.erase(existing->second.GetHash());
			qhIt->second.erase(existing);

			EquivocationRecord &rec = mapEquivocators[voterOutpoint];
			rec.count++;
			rec.lastEquivocationTime = now;

			return false;
		}
	}

	// Record the queue.
	mapQueues[q.nQueueHeight][voterOutpoint] = q;
	mapQueuesByHash[q.GetHash()] = q;

	if (fDebug)
	{
		LogPrintf("CMasternodeVoteTracker::ProcessQueue -- recorded queue from %s "
				  "for nQueueHeight %d\n",
				  voterOutpoint.ToString(), q.nQueueHeight);
	}

	(void)pfrom;
	return true;
}

bool CMasternodeVoteTracker::GetCanonicalWinnerFromQueues(int nTargetHeight, CScript &payeeOut)
{
	// CW5 (2026-05-31): canonical lock order is cs_main -> voteTracker.cs.
	// This function descends into CountVotingEligible -> IsVotingEligible
	// -> GetCollateralConfirmedHeight -> GetTransaction, which takes
	// cs_main.  Without holding cs_main here, any caller that did not
	// pre-acquire it would violate canonical order and ABBA against
	// ProcessGetData (which holds cs_main and may call GetQueueByHash
	// for voteTracker.cs).  Observed live in the 2026-05-31 wedge
	// (18h into soak; gdb capture proved Thread 18 (ThreadStakeMiner
	// via SignBlock -> CreateCoinStake -> GetEnforcedPayee) held
	// voteTracker.cs waiting on cs_main, while Thread 10
	// (ThreadMessageHandler -> ProcessGetData) held cs_main waiting
	// on voteTracker.cs).  Acquiring both locks here, in canonical
	// order, makes the function self-protecting against every current
	// and future caller -- CW0's per-call-site wrapper at miner.cpp:990
	// becomes structurally redundant (recursive re-acquire is a no-op
	// for boost::recursive_mutex) but is retained for explicit
	// documentation of intent.
	LOCK2(cs_main, cs);

	if (pindexBest == NULL)
	{
		return false;
	}

	// Commit-point gate (M1Q spec S9): no winner until the chain has reached
	// (nTargetHeight - VOTE_COMMIT_BUFFER).  Gives late queues time to
	// propagate before the read commits.
	int currentTip = pindexBest->nHeight;
	if (currentTip < nTargetHeight - VOTE_COMMIT_BUFFER)
	{
		return false;
	}

	// Denominator: eligible-voter count must be a pure function of
	// nTargetHeight and committed chain state (same rule as the per-height
	// GetCanonicalWinner -- CountVotingEligible, never CountEnabled).
	int eligibleVoters = mnodeman.CountVotingEligible(nTargetHeight, MIN_VOTING_PROTOCOL_VERSION);

	if (eligibleVoters < MIN_ENABLED_FOR_CONSENSUS)
	{
		if (fDebug)
		{
			LogPrintf("CMasternodeVoteTracker::GetCanonicalWinnerFromQueues -- below floor: "
					  "only %d eligible voters (< %d)\n",
					  eligibleVoters, MIN_ENABLED_FOR_CONSENSUS);
		}
		return false;
	}

	// Walk in-flight queue-heights newest-first.  The newest queue covering
	// nTargetHeight reflects the most recent chain state; older queues are
	// the fallback if the newer ones have not yet arrived.
	for (int qh = nTargetHeight - 1; qh >= nTargetHeight - VOTE_QUEUE_LENGTH; --qh)
	{
		std::map<int, std::map<COutPoint, CMasternodeVoteQueue> >::const_iterator qhIt =
				mapQueues.find(qh);

		if (qhIt == mapQueues.end())
		{
			continue;
		}

		int position = nTargetHeight - 1 - qh;   // 0 .. VOTE_QUEUE_LENGTH-1

		// Tally per-payee at this position across all voters at this qh.
		std::map<CScript, std::set<COutPoint> > tallyByPayee;

		for (std::map<COutPoint, CMasternodeVoteQueue>::const_iterator vit = qhIt->second.begin();
			 vit != qhIt->second.end(); ++vit)
		{
			const CMasternodeVoteQueue &q = vit->second;

			if (position >= 0 && position < (int)q.vPayeeQueue.size())
			{
				tallyByPayee[q.vPayeeQueue[position]].insert(vit->first);
			}
		}

		// Supermajority + uniqueness rule, identical to the per-height
		// GetCanonicalWinner: find payees clearing the threshold; require
		// exactly one.  Two clearers is an ambiguous (contested) position,
		// not a consensus -- skip to the next (older) queue-height.
		int nClearingCount = 0;
		int nBestVotes = -1;
		CScript bestPayee;

		for (std::map<CScript, std::set<COutPoint> >::const_iterator pit = tallyByPayee.begin();
			 pit != tallyByPayee.end(); ++pit)
		{
			int voteCount = (int)pit->second.size();

			bool clearsThreshold =
				((int64_t)voteCount * VOTED_CONSENSUS_THRESHOLD_DENOMINATOR >=
				 (int64_t)eligibleVoters * VOTED_CONSENSUS_THRESHOLD_NUMERATOR);

			if (!clearsThreshold)
			{
				continue;
			}

			nClearingCount++;

			if (voteCount > nBestVotes)
			{
				nBestVotes = voteCount;
				bestPayee = pit->first;
			}
		}

		if (nClearingCount == 1)
		{
			payeeOut = bestPayee;

			if (fDebug)
			{
				LogPrintf("CMasternodeVoteTracker::GetCanonicalWinnerFromQueues -- height %d: "
						  "consensus from queue-height %d position %d (%d/%d voters)\n",
						  nTargetHeight, qh, position, nBestVotes, eligibleVoters);
			}

			return true;
		}

		if (nClearingCount > 1)
		{
			// Ambiguous at this queue-height's position.  Do not name a
			// winner from it; try the next older queue-height.
			if (fDebug)
			{
				LogPrintf("CMasternodeVoteTracker::GetCanonicalWinnerFromQueues -- height %d: "
						  "queue-height %d position %d AMBIGUOUS (%d payees cleared), "
						  "trying older queue\n",
						  nTargetHeight, qh, position, nClearingCount);
			}
		}
		// nClearingCount == 0: no consensus at this queue-height; try older.
	}

	return false;
}

void CMasternodeVoteTracker::OnBlockConnectedQueues(int nBlockHeight)
{
	LOCK(cs);

	int pruneBelow = nBlockHeight - VOTE_PAST_HORIZON;

	for (std::map<int, std::map<COutPoint, CMasternodeVoteQueue> >::iterator it = mapQueues.begin();
		 it != mapQueues.end(); )
	{
		if (it->first < pruneBelow)
		{
			// Erase the by-hash entries for every queue at this height.
			const std::map<COutPoint, CMasternodeVoteQueue> &voters = it->second;
			for (std::map<COutPoint, CMasternodeVoteQueue>::const_iterator vit = voters.begin();
				 vit != voters.end(); ++vit)
			{
				mapQueuesByHash.erase(vit->second.GetHash());
			}

			mapQueues.erase(it++);
		}
		else
		{
			++it;
		}
	}
}

void CMasternodeVoteTracker::OnBlockDisconnectedQueues(int nBlockHeight)
{
	LOCK(cs);

	// M1Q spec S10.1: a queue cast at nQueueHeight == the disconnected height
	// was computed against an mapLastPaidHeight that included the
	// now-disconnected block's payment.  Erase those queues; the casting MN
	// will re-broadcast a fresh queue against the new ancestry on the next
	// block-connect.  Queues at nQueueHeight < the disconnected height were
	// computed against earlier (unaffected) state and remain valid.
	std::map<int, std::map<COutPoint, CMasternodeVoteQueue> >::iterator qhIt =
			mapQueues.find(nBlockHeight);

	if (qhIt != mapQueues.end())
	{
		const std::map<COutPoint, CMasternodeVoteQueue> &voters = qhIt->second;
		for (std::map<COutPoint, CMasternodeVoteQueue>::const_iterator vit = voters.begin();
			 vit != voters.end(); ++vit)
		{
			mapQueuesByHash.erase(vit->second.GetHash());
		}

		mapQueues.erase(qhIt);
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

CMasternodeVoteTracker::QueueInfo CMasternodeVoteTracker::GetQueueInfo(int nTargetHeight)
{
	QueueInfo qi;
	qi.height = nTargetHeight;
	qi.eligibleVoters = 0;
	qi.queueHeightUsed = -1;
	qi.position = -1;
	qi.totalQueues = 0;
	qi.hasConsensus = false;
	qi.canonicalVoteCount = 0;

	// Authoritative winner first -- never recompute the decision here, so the
	// RPC can never disagree with enforcement (GetCanonicalWinnerFromQueues
	// is the single source of truth).  It takes cs internally.
	CScript winner;
	qi.hasConsensus = GetCanonicalWinnerFromQueues(nTargetHeight, winner);
	if (qi.hasConsensus)
	{
		qi.canonicalPayee = winner;
	}

	// Breakdown (visibility only) under the same lock discipline.
	//
	// CW5 (2026-05-31): canonical lock order is cs_main -> voteTracker.cs.
	// The breakdown block below calls mnodeman.CountVotingEligible
	// (line 958), which descends into IsVotingEligible ->
	// GetCollateralConfirmedHeight -> GetTransaction (cs_main).  Taking
	// only voteTracker.cs here would violate canonical order and
	// re-introduce the same ABBA that the LOCK2 in
	// GetCanonicalWinnerFromQueues was added to prevent.  Same fix
	// shape: acquire both locks here, in canonical order, so this
	// function (reached via getvoteinfo RPC at rpcmnengine.cpp:1472)
	// cannot deadlock against ProcessGetData.
	LOCK2(cs_main, cs);

	if (pindexBest == NULL)
	{
		return qi;
	}

	qi.eligibleVoters = mnodeman.CountVotingEligible(nTargetHeight, MIN_VOTING_PROTOCOL_VERSION);

	// Mirror the newest-first walk: report the first (newest) queue-height
	// that actually has queues covering nTargetHeight.  This is the position
	// the consensus read would have looked at first.
	for (int qh = nTargetHeight - 1; qh >= nTargetHeight - VOTE_QUEUE_LENGTH; --qh)
	{
		std::map<int, std::map<COutPoint, CMasternodeVoteQueue> >::const_iterator qhIt =
				mapQueues.find(qh);

		if (qhIt == mapQueues.end())
		{
			continue;
		}

		int position = nTargetHeight - 1 - qh;

		std::map<CScript, std::set<COutPoint> > tallyByPayee;

		for (std::map<COutPoint, CMasternodeVoteQueue>::const_iterator vit = qhIt->second.begin();
			 vit != qhIt->second.end(); ++vit)
		{
			const CMasternodeVoteQueue &q = vit->second;

			if (position >= 0 && position < (int)q.vPayeeQueue.size())
			{
				tallyByPayee[q.vPayeeQueue[position]].insert(vit->first);
			}
		}

		if (tallyByPayee.empty())
		{
			continue;
		}

		qi.queueHeightUsed = qh;
		qi.position = position;
		qi.totalQueues = (int)qhIt->second.size();

		for (std::map<CScript, std::set<COutPoint> >::const_iterator pit = tallyByPayee.begin();
			 pit != tallyByPayee.end(); ++pit)
		{
			VoteInfoEntry e;
			e.payeeScript = pit->first;
			e.voterVins = pit->second;
			e.firstSeen = 0;   // queues are per-broadcast, no per-payee first-seen
			qi.perPayee.push_back(e);

			if (qi.hasConsensus && pit->first == qi.canonicalPayee)
			{
				qi.canonicalVoteCount = (int)pit->second.size();
			}
		}

		// Only the newest covering queue-height is reported (the one the
		// consensus read consults first).
		break;
	}

	return qi;
}




std::map<COutPoint, int>
CMasternodeVoteTracker::GetQueueVoterActivity() const
{
	LOCK(cs);
	std::map<COutPoint, int> activity;
	for (std::map<int, std::map<COutPoint, CMasternodeVoteQueue> >
			::const_iterator hIt = mapQueues.begin();
		 hIt != mapQueues.end(); ++hIt)
	{
		const int qh = hIt->first;
		for (std::map<COutPoint, CMasternodeVoteQueue>
				::const_iterator vIt = hIt->second.begin();
			 vIt != hIt->second.end(); ++vIt)
		{
			const COutPoint &voter = vIt->first;
			std::map<COutPoint, int>::iterator existing = activity.find(voter);
			if (existing == activity.end() || existing->second < qh)
			{
				activity[voter] = qh;
			}
		}
	}
	return activity;
}

// v2.0.0.8 PB-INFLIGHT REVERTED: GetConsensusCommittedHeights() removed.
// It iterated mapVotes (node-local, in-flight tally state) and was folded
// into FindOldestNotInVecChainDerived's payee selection -- making the
// vote a node casts depend on node-local state, which caused
// geographically separated masternode clusters to compute different
// winners (testnet 5/2 split, 2026-05-24).  The streak problem it was
// meant to fix did not exist on a correctly-configured fleet.  See the
// revert note in CMasternodeMan::FindOldestNotInVecChainDerived.

void ProcessMessageMasternodeVote(CNode *pfrom, std::string &strCommand, CDataStream &vRecv)
{
	if (fLiteMode)
	{
		return;
	}


	// =======================================================================
	// v2.0.0.8 M1Q -- queue-based voting message handler.
	//
	// "mnvotequeue" is the sole payment-consensus message post-Task-B (the
	// per-height "mnvote" handler was removed 2026-06-01).  Structure is
	// relay-before-validate to avoid black-holing peers that cannot
	// validate the queue locally (e.g. due to missing chain data), with an
	// amplification-DoS guard (junk signatures for a known voter are never
	// relayed) and a queue-length bound check (M1Q spec S18.2): a queue
	// whose length is not exactly VOTE_QUEUE_LENGTH is a protocol violation.
	// =======================================================================

	if (strCommand == "mnvotequeue")
	{
		CMasternodeVoteQueue q;

		try
		{
			vRecv >> q;
		}
		catch (std::exception &e)
		{
			LogPrintf("mnvotequeue -- failed to deserialize from peer %d: %s\n",
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

		// M1Q spec S18.2: enforce the protocol queue length exactly.  The
		// stream-level size guard already rejects an absurdly large message,
		// but the application layer enforces the protocol constant: a queue
		// of any length other than VOTE_QUEUE_LENGTH is malformed.  Checked
		// before anything else so a malformed queue is neither relayed nor
		// recorded.
		if ((int)q.vPayeeQueue.size() != VOTE_QUEUE_LENGTH)
		{
			LogPrintf("mnvotequeue -- reject: queue size %d != VOTE_QUEUE_LENGTH %d "
					  "from peer %d\n",
					  (int)q.vPayeeQueue.size(), VOTE_QUEUE_LENGTH,
					  pfrom ? pfrom->GetId() : -1);
			if (pfrom)
			{
				Misbehaving(pfrom->GetId(), 20);
			}
			return;
		}

		// Cheap guardrail 1: already seen -> already relayed, stop here.
		if (voteTracker.AlreadyHaveQueue(q.GetHash()))
		{
			return;
		}

		// Cheap guardrail 2: nQueueHeight must be within the accepted
		// window (same bounds philosophy ProcessQueue enforces).  A queue
		// cast too far ahead (beyond a peer being legitimately ahead of us
		// by the reorg buffer) or too far behind (old enough that none of
		// its positions are still useful) is neither relayed nor recorded.
		{
			int currentTip = pindexBest->nHeight;

			if (q.nQueueHeight > currentTip + REORG_DEPTH_BUFFER ||
				q.nQueueHeight < currentTip - VOTE_PAST_HORIZON)
			{
				if (fDebug)
				{
					LogPrintf("mnvotequeue -- nQueueHeight %d outside window (tip %d), "
							  "not relaying or recording\n",
							  q.nQueueHeight, currentTip);
				}

				return;
			}
		}

		// Relay-before-validate, split exactly as the mnvote handler:
		// relay on the uncheckable (voter==NULL) branch so an incomplete-MN-
		// list node does not suppress the queue for the fleet; otherwise
		// relay only AFTER the signature verifies, so a junk-signature queue
		// for a known voter never propagates.

		CMasternode *voter = mnodeman.Find(q.voterVin);
		if (voter == NULL)
		{
			// Uncheckable: relay so the queue is not suppressed for peers
			// that DO know the voter, then ask for the missing dsee.
			{
				CInv inv(MSG_MASTERNODE_VOTE_QUEUE, q.GetHash());
				std::vector<CInv> vInv;
				vInv.push_back(inv);

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

			if (fDebug)
			{
				LogPrintf("mnvotequeue -- unknown voter %s at nQueueHeight %d, relayed; "
						  "asking for dsee\n",
						  q.voterVin.prevout.ToString(), q.nQueueHeight);
			}
			if (pfrom)
			{
				mnodeman.AskForMN(pfrom, q.voterVin);
			}
			return;
		}

		if (!q.CheckSignature(voter->pubkey2))
		{
			// Same low-score rationale as the mnvote handler: a stale local
			// MN-list entry (voter re-registered with a new key we have not
			// yet processed) makes an honest queue fail here, so the score
			// is low.  A junk-signature queue is NOT relayed (relay for the
			// checkable case is below, after this check), so junk signatures
			// do not propagate regardless of score.
			LogPrintf("mnvotequeue -- invalid signature from %s nQueueHeight %d (peer %d)\n",
					  q.voterVin.prevout.ToString(), q.nQueueHeight,
					  pfrom ? pfrom->GetId() : -1);
			if (pfrom)
			{
				Misbehaving(pfrom->GetId(), 5);
			}
			return;
		}

		// Signature verified -- relay now (relay-after-validate for the
		// checkable case).  Only signature-valid queues ever reach here.
		{
			CInv inv(MSG_MASTERNODE_VOTE_QUEUE, q.GetHash());
			std::vector<CInv> vInv;
			vInv.push_back(inv);

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

		bool added = voteTracker.ProcessQueue(q, pfrom);

		if (!added)
		{
			return;
		}

		LogPrint("masternode", "mnvotequeue -- accepted queue from %s for nQueueHeight %d (peer %d)\n",
				  q.voterVin.prevout.ToString(), q.nQueueHeight,
				  pfrom ? pfrom->GetId() : -1);

		return;
	}

	if (strCommand == "getmnqueues")
	{
		voteTracker.SyncQueues(pfrom);
		return;
	}
}
