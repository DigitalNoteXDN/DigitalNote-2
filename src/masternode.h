#ifndef MASTERNODE_H
#define MASTERNODE_H

#include <climits>

class uint256;

#define MASTERNODE_NOT_PROCESSED		0 // initial state
#define MASTERNODE_IS_CAPABLE			1
#define MASTERNODE_NOT_CAPABLE			2
#define MASTERNODE_STOPPED				3
#define MASTERNODE_INPUT_TOO_NEW		4
#define MASTERNODE_PORT_NOT_OPEN		6
#define MASTERNODE_PORT_OPEN			7
#define MASTERNODE_SYNC_IN_PROCESS		8
#define MASTERNODE_REMOTELY_ENABLED		9

#define MASTERNODE_MIN_CONFIRMATIONS	7
#define MASTERNODE_MIN_DSEEP_SECONDS	(30*60)
#define MASTERNODE_MIN_DSEE_SECONDS		(5*60)
#define MASTERNODE_PING_SECONDS			(1*60)
#define MASTERNODE_EXPIRATION_SECONDS	(65*60)
#define MASTERNODE_REMOVAL_SECONDS		(70*60)

// ---------------------------------------------------------------------------
// v2.0.0.8 voted-payment consensus constants
//
// These govern the masternode-voted payment selection system introduced in
// v2.0.0.8.  Full design rationale lives in PhaseC-design.md; brief notes:
//
//   VOTED_CONSENSUS_ACTIVATION_HEIGHT
//     Block height at which validators begin enforcing the voted-consensus
//     payment rule.  Pre-activation: existing behaviour (any valid MN
//     payee accepted).  Post-activation: payee must match canonical voted
//     winner if consensus exists, else permissive fallback.
//
//     For M0/M1/M2/M3/M4 development builds: set to INT_MAX so enforcement
//     never triggers and the wallet runs identically to v2.0.0.7.
//     For M6 pre-release testing: still INT_MAX.
//     For M7 release: set to release_height 1480000 (7 months)
//
//   VOTE_LOOKAHEAD
//     Number of blocks ahead each masternode votes for.  An MN observing
//     block N broadcasts a vote for block N + VOTE_LOOKAHEAD.  Matches the
//     historical +10 used by the (broken) CMasternodePayments::ProcessBlock.
//
//   VOTE_PAST_HORIZON
//     Votes for heights below (currentHeight - VOTE_PAST_HORIZON) are
//     rejected as too old.  Lets late votes still be useful for a few blocks
//     after the height they apply to.
//
//   VOTE_TIME_WINDOW_SECONDS
//     Maximum acceptable skew between vote.nTimeSigned and local clock.
//     Matches the existing ±30min tolerance used by dsee/dseep.
//
//   REORG_DEPTH_BUFFER
//     When computing vote inputs (lastPaidHeight from chain), use chain
//     state at (currentHeight - REORG_DEPTH_BUFFER) for stability.  Handles
//     typical 1-block reorgs comfortably.  Deep reorgs (>10 blocks) trigger
//     vote-tracker cache clearing as a fallback.
//
//   MIN_ENABLED_FOR_CONSENSUS
//     Sanity floor.  Below this many enabled MNs, no canonical winner can
//     form and the permissive fallback rule applies unconditionally.
//
//   VOTED_CONSENSUS_THRESHOLD_NUMERATOR / VOTED_CONSENSUS_THRESHOLD_DENOMINATOR
//     Integer expression of the 60% threshold.  Implemented as
//     (votes * DENOM >= total * NUMER) to avoid floating-point.
//     3/5 == 60%.
//
//   MAX_EQUIVOCATIONS_PER_SESSION
//     After this many equivocation events from the same MN, fresh dsee no
//     longer clears equivocator status (Path A of S14.3 disabled).  Operator
//     must use explicit RPC (Path B) to clear.
//
//   MIN_VOTING_PROTOCOL_VERSION
//     Minimum peer protocol version that counts toward the consensus
//     denominator.
//
//     TEMPORARY: set to 62057 for the debug-build wedge-reproduction soak
//     so the test staker can form quorum with the rest of the fleet (which
//     is at 62057).  See ledger S24 / S25.  RETURN TO 62058 before release
//     -- the production rationale (queue denominator counts ONLY M1Q
//     queue-capable MNs in lockstep with PROTOCOL_VERSION) is unchanged.
// ---------------------------------------------------------------------------

#define VOTED_CONSENSUS_ACTIVATION_HEIGHT				1480000
#define VOTE_LOOKAHEAD									10
#define VOTE_PAST_HORIZON								10
#define VOTE_TIME_WINDOW_SECONDS						(30 * 60)
#define REORG_DEPTH_BUFFER								10
#define MIN_ENABLED_FOR_CONSENSUS						5
#define VOTED_CONSENSUS_THRESHOLD_NUMERATOR				3
#define VOTED_CONSENSUS_THRESHOLD_DENOMINATOR			5
#define MAX_EQUIVOCATIONS_PER_SESSION					3
#define MIN_VOTING_PROTOCOL_VERSION						62058

// v2.0.0.8 M1Q -- queue-based voting.
//   VOTE_QUEUE_LENGTH
//     The number of forward payee positions each queue carries.  Position
//     p (0-indexed) predicts the payee for height (nQueueHeight + 1 + p).
//     Equal to VOTE_LOOKAHEAD so that any height is covered by up to
//     VOTE_LOOKAHEAD in-flight queues after warm-up.  See
//     v208-M1Q-queue-based-voting-SPEC.md S4.
//   VOTE_COMMIT_BUFFER
//     GetCanonicalWinnerFromQueues returns no winner until the chain has
//     reached (targetHeight - VOTE_COMMIT_BUFFER).  Gives late-arriving
//     queues time to propagate before the consensus read commits.  See
//     spec S9.  Reorgs shallower than this preserve in-flight commits;
//     deeper reorgs may disturb them (spec S10.2).
#define VOTE_QUEUE_LENGTH								VOTE_LOOKAHEAD
#define VOTE_COMMIT_BUFFER								3

// Maximum depth the initial chain-walk for mapLastPaidHeight will look back.
// At observed 3.23min/block, 50000 blocks is ~112 days of history -- enough
// to find a recent payment for every active MN.
#define MAX_LASTPAID_SCAN_DEPTH							50000

// ---------------------------------------------------------------------------
// VOTER_ELIGIBILITY_DEPTH
//   Minimum chain depth, relative to the block being voted on, that a
//   masternode's 2,000,000 XDN collateral must have before that masternode
//   counts toward the voted-consensus denominator.
//
//   = MASTERNODE_MIN_CONFIRMATIONS (collateral maturity, 7) +
//     REORG_DEPTH_BUFFER          (reorg-stability margin, 10)
//
//   The buffer guarantees the eligible-voter set for height N cannot change
//   under any reorg shallower than REORG_DEPTH_BUFFER, which is the deepest
//   reorg the vote system itself tolerates (see VOTE_PAST_HORIZON / the
//   ProcessVote window).  This makes CountVotingEligible(N) a pure function
//   of committed chain state and therefore identical on every synced node --
//   the property GetCanonicalWinner requires to be deterministic.
// ---------------------------------------------------------------------------
#define VOTER_ELIGIBILITY_DEPTH							(MASTERNODE_MIN_CONFIRMATIONS + REORG_DEPTH_BUFFER)

bool GetBlockHash(uint256& hash, int nBlockHeight);

#endif // MASTERNODE_H
