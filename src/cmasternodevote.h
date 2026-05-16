#ifndef CMASTERNODEVOTE_H
#define CMASTERNODEVOTE_H

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
 * CMasternodeVote -- a vote by one masternode for the canonical payee at a
 * future block height.
 *
 * Wire-level message of type "mnvote".  Each enabled masternode broadcasts a
 * vote on every block-connect for height (currentHeight + VOTE_LOOKAHEAD).
 * Other nodes collect votes, deduplicate per-voter-per-height, and derive a
 * canonical winner when the threshold is reached.
 *
 * Distinct from CConsensusVote -- that class is for InstantX transaction-lock
 * voting (a separate feature inherited from the original Dash mnengine code).
 * This class is for masternode-payment selection voting introduced in v2.0.0.8.
 *
 * Design references:
 *   PhaseC-design.md  S7    message format
 *   PhaseC-design.md  S9    vote tally and equivocation detection
 *   PhaseC-design.md  S14.3 equivocation recovery paths
 */
class CMasternodeVote
{
public:
	CTxIn voterVin;                          // The voter MN's collateral vin (identity)
	int nBlockHeight;                        // The height being voted on
	CScript payeeScript;                     // The vote: who should be paid at nBlockHeight
	int64_t nTimeSigned;                     // When the vote was signed (replay/window check)
	std::vector<unsigned char> vchSig;       // Signature by voterVin's masternodeprivkey

	CMasternodeVote();
	CMasternodeVote(const CTxIn &vinIn, int nHeightIn, const CScript &payeeIn);

	uint256 GetHash() const;
	std::string GetSignableString() const;
	bool Sign(const std::string &strMnPrivKey);
	bool CheckSignature(const CPubKey &voterPubKey) const;

	// Standard project serialization pattern.  Note: we deliberately omit
	// the GetSerializeSize variant that CSporkMessage provides, because:
	//   1. Nothing in the codebase actually calls GetSerializeSize on a
	//      vote (CDataStream's operator<< / PushMessage path doesn't use it).
	//   2. Providing it would require additional Serialize<CSizeComputer, X>
	//      template instantiations for CTxIn and CScript in serialize/write.cpp
	//      that don't currently exist in the project (only primitives are
	//      instantiated against CSizeComputer).
	// If a future caller needs GetSerializeSize for votes, add it together
	// with the missing instantiations as a focused change.
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
	template<typename Stream, typename Operation>
	void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion);
};

#endif // CMASTERNODEVOTE_H
