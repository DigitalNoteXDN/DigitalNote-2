#ifndef BLOCKPARAMS_H
#define BLOCKPARAMS_H

#include <cstdint>

class CBlockIndex;

// v2.0.0.8 testnet-payment-constants:
//
// Masternode and DevOps payment toggles.  CreateCoinStake and the
// CheckBlock payment validator both branch on Params().NetworkID() and
// read the _TESTNET variants on testnet, the bare names on mainnet.
//
// Previously the four _TESTNET constants were all 9993058800 (year
// 2286) -- "OFF (NOT TOGGLED)".  That made testnet PoS DIVERGE from
// mainnet:
//   - mainnet START_* = 1554494400 (Apr 2019, in the past) -> payments
//     ON -> CreateCoinStake builds a 4- or 5-output coinstake (stake[s]
//     + masternode + devops) -> CheckBlock's "vout.size() must be 4 or
//     5" check passes.
//   - testnet START_* = 2286 -> payments OFF -> CreateCoinStake builds a
//     2- or 3-output coinstake -> CheckBlock rejects it ("PoS submission
//     doesn't include devops and/or masternode payment").
// Testnet PoS therefore could not produce a single valid block once the
// PB-1 staking crash was fixed: the chain stalled on the first PoS block
// (observed 2026-05-21, block 1193 rejected in a mint/reject loop).
//
// The testnet must replicate mainnet exactly.  The START_*_TESTNET
// constants are set to 1546300800 (1 Jan 2019 00:00:00 UTC) -- before
// testnet genesis (1547848830, 18 Jan 2019) -- so masternode and devops
// payments are active from testnet block 1, identically to mainnet.
// The STOP_*_TESTNET constants stay at 9993058800 (never stop), matching
// the mainnet STOP_* values.
//
// This is a testnet-configuration correction only.  No mainnet constant
// changes; no change to CreateCoinStake or CheckBlock logic -- those two
// were always consistent with each other, the testnet constants were the
// sole source of the divergence.
#define START_MASTERNODE_PAYMENTS_TESTNET	1546300800	// ON  (Tuesday, January 1, 2019 12:00:00 AM UTC) -- pre-testnet-genesis
#define START_MASTERNODE_PAYMENTS			1554494400	// ON  (Friday, April 5, 2019 1:00:00 PM GMT-07:00 | PDT)
#define STOP_MASTERNODE_PAYMENTS_TESTNET	9993058800	// OFF (never stop -- matches mainnet)
#define STOP_MASTERNODE_PAYMENTS			9993058800	// OFF (never stop)

#define START_DEVOPS_PAYMENTS_TESTNET		1546300800	// ON  (Tuesday, January 1, 2019 12:00:00 AM UTC) -- pre-testnet-genesis
#define START_DEVOPS_PAYMENTS				1554494400	// ON  (Friday, April 5, 2019 1:00:00 PM GMT-07:00 | PDT)
#define STOP_DEVOPS_PAYMENTS_TESTNET		9993058800	// OFF (never stop -- matches mainnet)
#define STOP_DEVOPS_PAYMENTS				9993058800	// OFF (never stop)

#define INSTANTX_SIGNATURES_REQUIRED		2
#define INSTANTX_SIGNATURES_TOTAL			4

// Define difficulty retarget algorithms
enum DiffMode
{
	DIFF_DEFAULT = 0, // Default to invalid 0
	DIFF_VRX = 1, // Retarget using Terminal-Velocity-RateX
};

void VRXswngdebug();
void VRXdebug();
void GNTdebug();
void VRX_BaseEngine(const CBlockIndex* pindexLast, bool fProofOfStake);
void VRX_Simulate_Retarget();
// v2.0.0.8 RESYNC FIX: nNewBlockTime is the timestamp of the block being
// targeted (the block at pindexLast->nHeight + 1).  The difficulty-recovery
// curve must measure stall time deterministically from this block timestamp,
// NOT from GetAdjustedTime() (wall clock), or the retarget becomes
// non-reproducible and historical blocks fail re-validation on resync.
// A value of 0 means "not supplied" -> fall back to GetAdjustedTime()
// (used by the mining path, where the new block's timestamp is not yet
// finalised and is ~now anyway).
void VRX_ThreadCurve(const CBlockIndex* pindexLast, bool fProofOfStake, int64_t nNewBlockTime = 0);
void VRX_Dry_Run(const CBlockIndex* pindexLast);
unsigned int VRX_Retarget(const CBlockIndex* pindexLast, bool fProofOfStake, int64_t nNewBlockTime = 0);
unsigned int GetNextTargetRequired(const CBlockIndex* pindexLast, bool fProofOfStake, int64_t nNewBlockTime = 0);
int64_t GetProofOfWorkReward(int nHeight, int64_t nFees);
int64_t GetProofOfStakeReward(const CBlockIndex* pindexPrev, int64_t nCoinAge, int64_t nFees);
int64_t GetMasternodePayment(int nHeight, int64_t blockValue);
int64_t GetDevOpsPayment(int nHeight, int64_t blockValue);

#endif // BLOCKPARAMS_H