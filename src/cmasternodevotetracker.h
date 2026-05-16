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

#include "cmasternodevote.h"

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
 * VoteRecord -- per-(height, payee) aggregation of voters.
 * The set of voter outpoints provides automatic deduplication.
 */
struct VoteRecord
{
	int nBlockHeight;
	CScript payeeScript;
	std::set<COutPoint> voterVins;
	int64_t nFirstSeen;

	VoteRecord() : nBlockHeight(0), nFirstSeen(0) {}
};

/**
 * CMasternodeVoteTracker -- collects votes, deduplicates per-voter-per-height,
 * detects equivocation, derives canonical winner when threshold is reached.
 *
 * Design references:
 *   PhaseC-design.md  S9    ProcessVote, GetCanonicalWinner
 *   PhaseC-design.md  S14.3 equivocation recovery (Path A/B/C)
 *   PhaseC-design.md  S17.5 reorg handling
 *
 * Implementation deferred to milestone M3 (see PhaseD-implementation.md S2).
 * M0 provides only the declaration so includes resolve and the build system
 * registers the file.
 */
class CMasternodeVoteTracker
{
public:
	mutable CCriticalSection cs;

	// Primary state: [height][payee] -> aggregated record
	std::map<int, std::map<CScript, VoteRecord> > mapVotes;

	// Per-voter-per-height dedup
	std::set<std::pair<int, COutPoint> > setSeenVoter;

	// Equivocation detection: voter -> (height, payee) for last seen vote
	std::map<COutPoint, std::pair<int, CScript> > mapEquivocationDetection;

	// Equivocator status: voter -> EquivocationRecord
	std::map<COutPoint, EquivocationRecord> mapEquivocators;

	CMasternodeVoteTracker();

	// Implemented in M3:
	bool ProcessVote(const CMasternodeVote &vote, CNode *pfrom);
	bool GetCanonicalWinner(int nBlockHeight, CScript &payeeOut);

	// Block lifecycle hooks (M3):
	void OnBlockConnected(int nBlockHeight);
	void OnBlockDisconnected(int nBlockHeight);

	// Equivocation recovery (M3):
	void OnFreshDsee(const COutPoint &voterVin);
	bool ClearEquivocator(const COutPoint &voterVin);
	bool IsEquivocator(const COutPoint &voterVin) const;

	// Internal helper (M3):
	void RemoveVoterVote(int nBlockHeight, const COutPoint &voterVin);

	// M3 inv-based relay support:
	// - mapVotesByHash maps GetHash() -> full CMasternodeVote so getdata responses
	//   can serve the actual vote bytes from our storage.
	// - AlreadyHaveVote tells main.cpp's AlreadyHave whether we already know
	//   a given inv-hash.
	// - GetVoteByHash retrieves a stored vote for getdata response.
	std::map<uint256, CMasternodeVote> mapVotesByHash;
	bool AlreadyHaveVote(const uint256 &hash) const;
	bool GetVoteByHash(const uint256 &hash, CMasternodeVote &voteOut) const;

	// M3 sync: when a peer connects we push them our recent votes via inv.
	// Sends invs for all stored votes within the active height range.
	void Sync(CNode *pnode);

	// M3 RPC support:
	// - GetEquivocatorList: snapshot of mapEquivocators for listequivocators RPC
	// - GetVoteInfo: per-height tally summary for getvoteinfo RPC
	struct EquivocatorInfo
	{
		COutPoint voterVin;
		int count;
		int64_t lastEquivocationTime;
		bool autoClearingAvailable;  // true if count < MAX_EQUIVOCATIONS_PER_SESSION
	};

	struct VoteInfoEntry
	{
		CScript payeeScript;
		std::set<COutPoint> voterVins;
		int64_t firstSeen;
	};

	struct VoteInfo
	{
		int height;
		int totalVotes;           // sum across all payees
		int eligibleVoters;       // CountEnabled filtered to MIN_VOTING_PROTOCOL_VERSION
		std::vector<VoteInfoEntry> perPayee;
		bool hasConsensus;
		CScript canonicalPayee;
		int canonicalVoteCount;
	};

	std::vector<EquivocatorInfo> GetEquivocatorList() const;
	VoteInfo GetVoteInfo(int nBlockHeight) const;

	// M3 patch 1: surface "which voters have voted recently".
	// Returns map of voter outpoint -> most recent height they voted for,
	// across all currently-tracked mapVotes state.  Voters NOT in the result
	// haven't voted within the active window (VOTE_PAST_HORIZON to
	// VOTE_LOOKAHEAD around tip) -- this is the diagnostic for "broken MN
	// is online enough to ping but not actually voting."  Returns local
	// view only; other nodes may see different activity depending on
	// network propagation and their own pruning state.
	std::map<COutPoint, int> GetVoterActivity() const;
};

extern CMasternodeVoteTracker voteTracker;

// v2.0.0.8 M2: message dispatcher for "mnvote" and "getmnvotes" commands.
// Called from main.cpp's main message dispatcher alongside ProcessSpork etc.
void ProcessMessageMasternodeVote(CNode *pfrom, std::string &strCommand, CDataStream &vRecv);

#endif // CMASTERNODEVOTETRACKER_H
