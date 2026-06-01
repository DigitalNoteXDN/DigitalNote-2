#ifndef CMASTERNODEMAN_H
#define CMASTERNODEMAN_H

#include <cstdint>
#include <vector>
#include <map>

#include "types/ccriticalsection.h"
#include "types/ctxdestination.h"
#include "cmnqueuesnapshot.h"

class CNode;
class CMasternode;
class CNetAddr;
class COutPoint;
class CService;
class CTxIn;
class CPubKey;
class CDataStream;
class CScript;
class CBlock;

class CMasternodeMan
{
private:
	// critical section to protect the inner data structures
	mutable CCriticalSection cs;

	// map to hold all MNs
	std::vector<CMasternode> vMasternodes;
	// who's asked for the masternode list and the last time
	std::map<CNetAddr, int64_t> mAskedUsForMasternodeList;
	// who we asked for the masternode list and the last time
	std::map<CNetAddr, int64_t> mWeAskedForMasternodeList;
	// which masternodes we've asked for
	std::map<COutPoint, int64_t> mWeAskedForMasternodeListEntry;

	// ----------------------------------------------------------------------
	// v2.0.0.8: chain-derived last-paid-height cache.
	//
	// mapLastPaidHeight[mn.vin.prevout] = the canonical chain height at which
	// this MN was most recently paid.  Populated by a bounded walk at startup
	// (PopulateLastPaidHeightCache) and maintained by OnBlockConnected /
	// OnBlockDisconnected hooks.
	//
	// NOT serialized to mncache.dat -- always rebuilt from chain on startup.
	// This guarantees v2.0.0.7 <-> v2.0.0.8 cache file compatibility and
	// avoids stale-data risk if cache and chain ever diverge.
	//
	// Replaces the broken per-wallet nLastPaid field (PhaseA-current-state.md
	// S1.5) as the input to FindOldestNotInVecChainDerived.  CMasternode's
	// own nLastPaid field is preserved for display purposes only.
	// ----------------------------------------------------------------------
	std::map<COutPoint, int> mapLastPaidHeight;

	// v2.0.0.8 PB-6: VESTIGIAL.  Formerly bounded RecomputeLastPaidHeight's
	// backward walk, but that bound was the PB-6 bug -- it is set to where
	// the startup scan terminated (a shallow recent height), which made
	// RecomputeLastPaidHeight miss payments deeper than that window.
	// RecomputeLastPaidHeight now walks by MAX_LASTPAID_SCAN_DEPTH instead.
	// This field is still written (constructor, PopulateLastPaidHeightCache)
	// but no longer read.  Left in place to avoid churn; do NOT reintroduce
	// it as a walk bound.
	int nLastPaidHeightScannedTo;

public:
	// keep track of dsq count to prevent masternodes from gaming mnengine queue
	int64_t nDsqCount;

	CMasternodeMan();
	CMasternodeMan(CMasternodeMan& other);

	// Add an entry
	bool Add(CMasternode &mn);

	// Check all masternodes
	void Check();

	/// Ask (source) node for mnb
	void AskForMN(CNode* pnode, CTxIn &vin);

	// Check all masternodes and remove inactive
	void CheckAndRemove();

	// Clear masternode vector
	void Clear();

	int CountEnabled(int protocolVersion = -1);

	// v2.0.0.8 voted-consensus: deterministic, chain-derived eligible-voter
	// count for a given block height.  Consensus-denominator counterpart of
	// CountEnabled().  See implementation for the full rationale.
	int CountVotingEligible(int nBlockHeight, int protocolVersion = -1);

	int CountMasternodesAboveProtocol(int protocolVersion);

	void DsegUpdate(CNode* pnode);

	// Find an entry
	CMasternode* Find(const CTxIn& vin);
	CMasternode* Find(const CPubKey& pubKeyMasternode);

	//Find an entry thta do not match every entry provided vector
	CMasternode* FindOldestNotInVec(const std::vector<CTxIn> &vVins, int nMinimumAge);

	// Find a random entry
	CMasternode* FindRandom();

	/// Find a random entry
	CMasternode* FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion = -1);

	// Get the current winner for this block
	CMasternode* GetCurrentMasterNode(int mod=1, int64_t nBlockHeight=0, int minProtocol=0);

	bool IsPayeeAValidMasternode(CScript payee);
	std::vector<CMasternode> GetFullMasternodeVector();

	std::vector<std::pair<int, CMasternode>> GetMasternodeRanks(int64_t nBlockHeight, int minProtocol=0);
	int GetMasternodeRank(const CTxIn &vin, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);
	CMasternode* GetMasternodeByRank(int nRank, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);

	void ProcessMasternodeConnections();

	void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

	std::string ToString() const;
	int size();

	// ----------------------------------------------------------------------
	// v2.0.0.8: chain-derived last-paid-height cache (M1)
	//
	// All methods are thread-safe (acquire cs internally).
	// ----------------------------------------------------------------------

	// Look up MN by the address its rewards are paid to.
	// Uses dest equality (works for both P2PK and P2PKH outputs).
	// Returns NULL if no MN's payment address matches.
	CMasternode* FindByPayeeAddress(const CTxDestination& dest);

	// Called from main.cpp after a block is connected at the tip.  Scans the
	// block's payment-bearing transaction (coinstake for PoS, coinbase for
	// PoW), identifies the MN payee by address match, and updates the cache.
	void OnBlockConnected(const CBlock& block, int nBlockHeight);

	// Called from main.cpp after a block is disconnected (reorg).  If the
	// disconnected block was an MN's last-known-payment block, triggers a
	// bounded walk to find the next-most-recent payment for that MN.
	void OnBlockDisconnected(const CBlock& block, int nBlockHeight);

	// Walk the chain backward from current tip, looking for the most recent
	// payment to the given MN.  Bounded by nLastPaidHeightScannedTo so the
	// walk terminates.  If no payment found in range, removes the cache
	// entry (MN is treated as "never paid in scanned range").
	void RecomputeLastPaidHeight(CMasternode* mn);

	// One-time at startup (after chain is loaded): walk backward up to
	// MAX_LASTPAID_SCAN_DEPTH blocks, recording the most recent payment for
	// each enabled MN.  Stops early if all enabled MNs are accounted for.
	void PopulateLastPaidHeightCache();

	// Return cached lastPaidHeight for an MN.  Returns 0 if not found in the
	// cache (which means "never paid in our scanned range" -- treated as
	// longest-ago-paid by FindOldestNotInVecChainDerived).
	int GetLastPaidHeight(const COutPoint& vinPrevout) const;

	// v2.0.0.8 M1Q: snapshot every MN's chain-derived payment state under a
	// single cs acquisition (spec S18.1), for the queue forward-simulation
	// in CActiveMasternode::BroadcastQueue.  Returns one entry per MN in
	// vMasternodes.  Keeping cs private and exposing this purpose-built
	// accessor preserves the manager's lock encapsulation -- external code
	// never holds mnodeman.cs directly.
	std::vector<CMnPaymentSnapshotEntry> GetQueuePaymentSnapshot() const;

	// Same selection semantics as FindOldestNotInVec, but uses chain-derived
	// lastPaidHeight instead of local nLastPaid.  Deterministic across nodes
	// with the same chain state.
	//
	// nReferenceHeight is the upper bound on "recently paid" -- payments at
	// heights > nReferenceHeight are ignored (reorg-protection).  Callers
	// should pass (currentHeight - REORG_DEPTH_BUFFER) for vote production.
	//
	// Tie-breaking: when multiple MNs share the same lastPaidHeight, pick
	// the one with the lowest vin.prevout (grind-resistant per PhaseB B3.3).
	//
	// NOT YET CALLED FROM SELECTION PATH -- that wiring lands in M5.
	//
	// v2.0.0.8 Fix C: fChainDerivedEligibility selects the candidate-pool
	// eligibility predicate.  When true (the vote path -- BroadcastVote),
	// candidates are filtered by the deterministic, chain-derived
	// CMasternode::IsVotingEligible(nReferenceHeight) so two nodes
	// computing a vote for the same height pick the same candidate set
	// regardless of differing wall-clock liveness views.  When false
	// (default -- legacy / non-consensus callers), the original
	// IsEnabled() liveness filter is used.
	CMasternode* FindOldestNotInVecChainDerived(const std::vector<CTxIn>& vVins,
												int nMinimumAge,
												int nReferenceHeight,
												bool fChainDerivedEligibility = false);

	//
	// Relay Masternode Messages
	//

	void RelayMasternodeEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey,
			const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion, CScript donationAddress, int donationPercentage);
	void RelayMasternodeEntryPing(const CTxIn vin, const std::vector<unsigned char> vchSig, const int64_t nNow, const bool stop);

	void Remove(CTxIn vin);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CMASTERNODEMAN_H
