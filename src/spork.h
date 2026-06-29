#ifndef SPORK_H
#define SPORK_H

#include <cstdint>
#include <map>
#include <string>

#include "csporkmanager.h"

// Don't ever reuse these IDs for other sporks
#define SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT		10000
#define SPORK_2_INSTANTX							10001
#define SPORK_3_INSTANTX_BLOCK_FILTERING			10002
#define SPORK_4_NOTUSED								10003
#define SPORK_5_MAX_VALUE							10004
#define SPORK_6_REPLAY_BLOCKS						10005
#define SPORK_7_MASTERNODE_SCANNING					10006
#define SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT		10007
#define SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT		10008
#define SPORK_10_MASTERNODE_PAY_UPDATED_NODES		10009
#define SPORK_11_RESET_BUDGET						10010
#define SPORK_12_RECONSIDER_BLOCKS					10011
#define SPORK_13_ENABLE_SUPERBLOCKS					10012
// SPORK_14_TEST_SIGNATURES is reserved for spork signature key validation
// and protocol-level testing.  Do NOT connect this spork to any code path.
// Future versions must not repurpose this ID for a functional spork --
// reuse would silently activate features based on stale test broadcasts
// stored in nodes' mapSporksActive.  If a new functional spork is needed,
// allocate a fresh ID (SPORK_15+).
#define SPORK_14_TEST_SIGNATURES					10013

// SPORK_15 controls activation of masternode-voted payment consensus (M4).
// Semantics differ from time-window sporks above: this value is a BLOCK HEIGHT
// override, not a timestamp gate.
// - 0 (default): no spork override; activation height is the hardcoded floor
//   (see GetEffectiveVotedConsensusActivationHeight in cblock.cpp).
// - >0: activation at min(hardcoded_floor, spork_value).  The min() prevents
//   a compromised spork key from activating retroactively or pushing the gate
//   past the hardcoded floor.  Set this BELOW the hardcoded floor to advance
//   activation, never above.
#define SPORK_15_VOTED_CONSENSUS_ACTIVATION			10014

#define SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT_DEFAULT		4070908800		// OFF
#define SPORK_2_INSTANTX_DEFAULT							0				// ON
#define SPORK_3_INSTANTX_BLOCK_FILTERING_DEFAULT			0				// ON
#define SPORK_4_RECONVERGE_DEFAULT							0				// ON - BUT NOT USED
#define SPORK_5_MAX_VALUE_DEFAULT							3000000			// 3,000,000 XDN
#define SPORK_6_REPLAY_BLOCKS_DEFAULT						0				// ON - BUT NOT USED
#define SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT		4070908800		// OFF
#define SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT_DEFAULT		4070908800		// OFF
#define SPORK_10_MASTERNODE_PAY_UPDATED_NODES_DEFAULT		4070908800		// OFF
#define SPORK_11_RESET_BUDGET_DEFAULT						0				// ON
#define SPORK_12_RECONSIDER_BLOCKS_DEFAULT					0				// ON
#define SPORK_13_ENABLE_SUPERBLOCKS_DEFAULT					4070908800		// OFF
#define SPORK_14_TEST_SIGNATURES_DEFAULT					4070908800		// OFF (default never matters -- spork is not connected to any code)
#define SPORK_15_VOTED_CONSENSUS_ACTIVATION_DEFAULT			0				// 0 = no override (use hardcoded floor)

class CSporkMessage;
class uint256;
class CDataStream;
class CNode;

extern std::map<uint256, CSporkMessage> mapSporks;
extern std::map<int, CSporkMessage> mapSporksActive;
extern CSporkManager sporkManager;

void ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
int64_t GetSporkValue(int nSporkID);
bool IsSporkActive(int nSporkID);
void ExecuteSpork(int nSporkID, int nValue);
//void ReprocessBlocks(int nBlocks);

#endif // SPORK_H
