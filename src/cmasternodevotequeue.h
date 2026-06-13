#ifndef CMASTERNODEVOTEQUEUE_H
#define CMASTERNODEVOTEQUEUE_H

#include <cstdint>
#include <vector>
#include <string>

#include "ctxin.h"
#include "cscript.h"
#include "uint/uint256.h"
#include "serialize.h"

class CKey;
class CPubKey;

/**
 * CMasternodeVoteQueue -- a queue-based vote by one masternode predicting the
 * canonical payees at the next VOTE_QUEUE_LENGTH block heights.
 *
 * Wire-level message of type "mnvotequeue".  Replaces the per-height
 * CMasternodeVote design (M1Q, 2026-05-27) -- see
 * v208-M1Q-queue-based-voting-SPEC.md.
 *
 * On every block-connect, each enabled masternode broadcasts ONE queue
 * computed as a deterministic simulation of the rotation forward from the
 * current chain tip: position p (0-indexed) of the queue is the predicted
 * payee for height (nQueueHeight + 1 + p).
 *
 * Because the queue is a pure function of chain-derived state
 * (mapLastPaidHeight + IsVotingEligible + collateral confirmation heights),
 * every honest MN computes the SAME queue.  Consensus per-position is
 * trivially full (7/7 on a 7-MN fleet).  The payee for any given height N
 * is read via GetCanonicalWinnerFromQueues(N), which walks the in-flight queues
 * covering N newest-first and returns the first one with supermajority
 * agreement at the relevant position.
 *
 * This design replaces the per-height point vote because the point-vote
 * design produces VOTE_LOOKAHEAD-length payment streaks in steady state:
 * the selector picks the lowest-mapLastPaidHeight MN, that MN remains
 * lowest for the VOTE_LOOKAHEAD-block latency between vote and payment,
 * so the selector picks the same MN every vote until its first payment
 * lands.  See ledger sections 16-18 and the M1Q spec for the full
 * mechanism analysis.
 *
 * Equivocation: two distinct queues from the same voter at the same
 * nQueueHeight are equivocation regardless of content.  A queue from the
 * same voter at a different nQueueHeight is a normal per-block recompute.
 *
 * Distinct from CMasternodeVote (the predecessor point-vote class), which
 * is deprecated post-activation but kept for one release as defensive
 * deserialization.  Distinct from CConsensusVote (InstantX transaction-lock
 * voting -- separate feature inherited from upstream).
 */
class CMasternodeVoteQueue
{
public:
	CTxIn voterVin;                              // The voter MN's collateral vin (identity)
	int nQueueHeight;                            // Chain tip at the moment this queue was computed
	std::vector<CScript> vPayeeQueue;            // Ordered payees; vPayeeQueue[p] -> height nQueueHeight+1+p
	int64_t nTimeSigned;                         // When the queue was signed (replay/window check)
	std::vector<unsigned char> vchSig;           // Signature by voterVin's masternodeprivkey

	CMasternodeVoteQueue();
	CMasternodeVoteQueue(const CTxIn &vinIn, int nQueueHeightIn,
	                     const std::vector<CScript> &vPayeeQueueIn);

	uint256 GetHash() const;
	std::string GetSignableString() const;
	bool Sign(const std::string &strMnPrivKey);
	bool CheckSignature(const CPubKey &voterPubKey) const;

	// Standard project serialization pattern -- mirrors CMasternodeVote.
	// GetSerializeSize variant omitted for the same reason as CMasternodeVote:
	// no caller uses it, and providing it would require additional
	// Serialize<CSizeComputer, X> template instantiations not currently in
	// the project.  If a future caller needs it, add together with the
	// missing instantiations.
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
	template<typename Stream, typename Operation>
	void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion);
};

#endif // CMASTERNODEVOTEQUEUE_H
