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
//     For M7 release: set to release_height + ~80,000 blocks (~6 months at
//     observed 3.23 min/block rate).
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
//     denominator.  Set equal to v2.0.0.8's PROTOCOL_VERSION (62056).
// ---------------------------------------------------------------------------

#define VOTED_CONSENSUS_ACTIVATION_HEIGHT				INT_MAX
#define VOTE_LOOKAHEAD									10
#define VOTE_PAST_HORIZON								10
#define VOTE_TIME_WINDOW_SECONDS						(30 * 60)
#define REORG_DEPTH_BUFFER								10
#define MIN_ENABLED_FOR_CONSENSUS						5
#define VOTED_CONSENSUS_THRESHOLD_NUMERATOR				3
#define VOTED_CONSENSUS_THRESHOLD_DENOMINATOR			5
#define MAX_EQUIVOCATIONS_PER_SESSION					3
#define MIN_VOTING_PROTOCOL_VERSION						62056

// Maximum depth the initial chain-walk for mapLastPaidHeight will look back.
// At observed 3.23min/block, 50000 blocks is ~112 days of history -- enough
// to find a recent payment for every active MN.
#define MAX_LASTPAID_SCAN_DEPTH							50000

bool GetBlockHash(uint256& hash, int nBlockHeight);

#endif // MASTERNODE_H
