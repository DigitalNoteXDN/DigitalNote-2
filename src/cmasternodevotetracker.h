#ifndef CMASTERNODEVOTETRACKER_H
#define CMASTERNODEVOTETRACKER_H

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "types/ccriticalsection.h"

#include "ctxin.h"
#include "cscript.h"
#include "uint/uint256.h"

#include "cmasternodevotequeue.h"

class CBlock;
class CNode;
class COutPoint;
class CDataStream;

/**
 * EquivocationRecord -- bookkeeping for masternodes that have signed
 * conflicting votes.  See PhaseC-design.md S14.3.
 */
struct EquivocationRecord
{
	int count;                       // Times equivocated this session
	int64_t lastEquivocationTime;

	EquivocationRecord() : count(0), lastEquivocationTime(0) {}
};

/**
 * CMasternodeVoteTracker -- v2.0.0.8 M1Q queue-based voting tracker.
 *
 * Collects per-voter ordered payee queues for upcoming heights,
 * detects equivocation (two distinct queues from the same voter at the
 * same nQueueHeight), and exposes a queue-based canonical-winner
 * lookup for enforcement.
 *
 * Storage shape: mapQueues[nQueueHeight][voterVin] -> CMasternodeVoteQueue.
 * One queue per voter per nQueueHeight; any second distinct queue is
 * equivocation (M1Q spec S8).
 *
 * Design references:
 *   v208-M1Q-queue-based-voting-SPEC.md
 *   PhaseC-design.md S14.3 (equivocation recovery -- still applicable)
 *   PhaseC-design.md S17.5 (reorg handling -- still applicable)
 */
class CMasternodeVoteTracker
{
public:
	mutable CCriticalSection cs;

	// Equivocation detection: voter -> (height, payee) for last seen vote
	std::map<COutPoint, std::pair<int, CScript> > mapEquivocationDetection;

	// Equivocator status: voter -> EquivocationRecord
	std::map<COutPoint, EquivocationRecord> mapEquivocators;

	CMasternodeVoteTracker();

	// Equivocation recovery (M3):
	void OnFreshDsee(const COutPoint &voterVin);
	bool ClearEquivocator(const COutPoint &voterVin);
	bool IsEquivocator(const COutPoint &voterVin) const;

	// =====================================================================
	// v2.0.0.8 M1Q -- queue-based voting state and interface.
	//
	// These REPLACE the per-height mapVotes path post-activation.  Storage
	// shape: [nQueueHeight][voterVin] -> the voter's queue.  ONE queue per
	// voter per nQueueHeight; a second distinct queue from the same voter
	// at the same nQueueHeight is equivocation (M1Q spec S8).
	//
	// Definitions land in M1Q step 3 (this declaration block is added in
	// step 2 so the message-handler references resolve).  See
	// v208-M1Q-queue-based-voting-SPEC.md.
	// =====================================================================
	std::map<int, std::map<COutPoint, CMasternodeVoteQueue> > mapQueues;
	std::map<uint256, CMasternodeVoteQueue> mapQueuesByHash;

	// Receive path:
	bool ProcessQueue(const CMasternodeVoteQueue &q, CNode *pfrom);

	// Consensus read: walk in-flight queues covering nTargetHeight
	// newest-first, return the first with supermajority agreement at the
	// relevant position.
	bool GetCanonicalWinnerFromQueues(int nTargetHeight, CScript &payeeOut);

	// Block lifecycle (queue pruning / reorg invalidation):
	void OnBlockConnectedQueues(int nBlockHeight);
	void OnBlockDisconnectedQueues(int nBlockHeight);

	// Inv-relay support (mirrors the vote equivalents):
	bool AlreadyHaveQueue(const uint256 &hash) const;
	bool GetQueueByHash(const uint256 &hash, CMasternodeVoteQueue &queueOut) const;

	// Peer sync (mirrors Sync for queues):
	void SyncQueues(CNode *pnode);

	// M3 RPC support:
	// - GetEquivocatorList: snapshot of mapEquivocators for listequivocators RPC
	struct EquivocatorInfo
	{
		COutPoint voterVin;
		int count;
		int64_t lastEquivocationTime;
		bool autoClearingAvailable;  // true if count < MAX_EQUIVOCATIONS_PER_SESSION
	};

	// Per-payee tally entry, reused by GetQueueInfo (M1Q getvoteinfo RPC).
	struct VoteInfoEntry
	{
		CScript payeeScript;
		std::set<COutPoint> voterVins;
		int64_t firstSeen;
	};

	std::vector<EquivocatorInfo> GetEquivocatorList() const;

	// M1Q: queue-path analogue of GetVoteInfo, for the getvoteinfo RPC.
	// Reports the per-payee tally at the position covering nTargetHeight,
	// taken from the NEWEST queue-height that has any queues (mirrors the
	// newest-first walk in GetCanonicalWinnerFromQueues).  The authoritative
	// winner/has_consensus is taken from GetCanonicalWinnerFromQueues itself
	// (not recomputed here) so the RPC can never disagree with enforcement.
	// Visibility only -- does not make a consensus decision.
	struct QueueInfo
	{
		int height;               // nTargetHeight queried
		int eligibleVoters;       // CountVotingEligible(nTargetHeight)
		int queueHeightUsed;      // the qh whose position was tallied (-1 if none)
		int position;             // position within the queue (-1 if none)
		int totalQueues;          // number of queues tallied at that qh
		std::vector<VoteInfoEntry> perPayee;   // payee -> voters at that position
		bool hasConsensus;        // from GetCanonicalWinnerFromQueues
		CScript canonicalPayee;   // valid only if hasConsensus
		int canonicalVoteCount;   // voters for the canonical payee at the position
	};
	QueueInfo GetQueueInfo(int nTargetHeight);

	// Queue-path voter activity (used by getmnlastpaid RPC).
	// Returns: voterOutpoint -> highest queue-height that voter has
	// broadcast a queue for, walking mapQueues. Used as the streak-
	// activity column in the masternode list display.
	std::map<COutPoint, int> GetQueueVoterActivity() const;
};

extern CMasternodeVoteTracker voteTracker;

// v2.0.0.8 M1Q (post-Task-B, 2026-06-01): message dispatcher for the
// "mnvotequeue" and "getmnqueues" commands.  Called from main.cpp's
// main message dispatcher alongside ProcessSpork etc.  Pre-Task-B this
// also handled the per-height "mnvote" and "getmnvotes" path; those
// were removed when the queue mechanism became the sole consensus path.
void ProcessMessageMasternodeVote(CNode *pfrom, std::string &strCommand, CDataStream &vRecv);

#endif // CMASTERNODEVOTETRACKER_H
