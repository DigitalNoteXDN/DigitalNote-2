#include "compat.h"

#include <boost/lexical_cast.hpp>

#include "util.h"
#include "hash.h"
#include "ckey.h"
#include "cpubkey.h"
#include "cdatastream.h"
#include "cmnenginesigner.h"
#include "mnengine_extern.h"

#include "cmasternodevote.h"

/*
 * Implementation references:
 *
 * - PhaseC-design.md S7 specifies the canonical signable form as
 *   (voterVin || nBlockHeight || payeeScript || nTimeSigned), each formatted
 *   the same way the legacy CConsensusVote and CMasternodePayments::Sign do
 *   (string concatenation via boost::lexical_cast and component ToString()).
 *
 * - Signing uses mnEngineSigner.{SetKey, SignMessage, VerifyMessage} -- the
 *   same primitives as dsee/dseep/mnw, which means the signing key (the
 *   strMasterNodePrivKey loaded at init from masternodeprivkey) and the
 *   verification pubkey (CMasternode::pubkey2) are reused unchanged.  No new
 *   key infrastructure introduced by v2.0.0.8.
 */

CMasternodeVote::CMasternodeVote()
	: nBlockHeight(0), nTimeSigned(0)
{
}

CMasternodeVote::CMasternodeVote(const CTxIn &vinIn, int nHeightIn, const CScript &payeeIn)
	: voterVin(vinIn), nBlockHeight(nHeightIn), payeeScript(payeeIn), nTimeSigned(0)
{
}

std::string CMasternodeVote::GetSignableString() const
{
	// Canonical signable representation.  Format chosen to match the
	// existing CMasternodePayments::Sign convention (rpcmnengine et al
	// follow this pattern for signed-message inputs).
	std::string strMessage =
		voterVin.ToString() +
		boost::lexical_cast<std::string>(nBlockHeight) +
		payeeScript.ToString() +
		boost::lexical_cast<std::string>(nTimeSigned);

	return strMessage;
}

uint256 CMasternodeVote::GetHash() const
{
	// Inv-mechanism hash.  Combines all signable fields so that even
	// equivocating votes (same voter+height but different payee) hash
	// distinctly -- both copies travel the network and the receiver sees the
	// equivocation rather than treating the second as a duplicate.
	CDataStream ss(SER_GETHASH, 0);

	ss << voterVin;
	ss << nBlockHeight;
	ss << payeeScript;
	ss << nTimeSigned;

	return Hash(ss.begin(), ss.end());
}

bool CMasternodeVote::Sign(const std::string &strMnPrivKey)
{
	if (nTimeSigned == 0)
	{
		nTimeSigned = GetAdjustedTime();
	}

	CKey key;
	CPubKey pubkey;
	std::string errorMessage;

	if (!mnEngineSigner.SetKey(strMnPrivKey, errorMessage, key, pubkey))
	{
		LogPrintf("CMasternodeVote::Sign -- SetKey failed: %s\n", errorMessage.c_str());

		return false;
	}

	std::string strMessage = GetSignableString();

	if (!mnEngineSigner.SignMessage(strMessage, errorMessage, vchSig, key))
	{
		LogPrintf("CMasternodeVote::Sign -- SignMessage failed: %s\n", errorMessage.c_str());

		return false;
	}

	// Self-verify guard.  Same pattern as CMasternodePayments::Sign and
	// CConsensusVote::Sign -- catches signature corruption immediately
	// rather than letting an invalid vote travel the network.
	if (!mnEngineSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage))
	{
		LogPrintf("CMasternodeVote::Sign -- self-verify failed: %s\n", errorMessage.c_str());

		return false;
	}

	return true;
}

bool CMasternodeVote::CheckSignature(const CPubKey &voterPubKey) const
{
	std::string strMessage = GetSignableString();
	std::string errorMessage;

	// The project's CMNengineSigner::VerifyMessage takes vchSig by non-const
	// reference (legacy API).  We're const, so make a copy.  Cost is trivial
	// (vchSig is ~65 bytes for an ECDSA signature).
	std::vector<unsigned char> vchSigCopy = vchSig;

	if (!mnEngineSigner.VerifyMessage(voterPubKey, vchSigCopy, strMessage, errorMessage))
	{
		LogPrintf("CMasternodeVote::CheckSignature -- verify failed for voter %s height %d: %s\n",
				  voterVin.prevout.ToString(), nBlockHeight, errorMessage.c_str());

		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Serialization (standard project pattern, mirrors CSporkMessage minus the
// unused GetSerializeSize variant -- see header comment).
// ---------------------------------------------------------------------------

template<typename Stream>
void CMasternodeVote::Serialize(Stream& s, int nType, int nVersion) const
{
	NCONST_PTR(this)->SerializationOp(s, CSerActionSerialize(), nType, nVersion);
}

template void CMasternodeVote::Serialize<CDataStream>(CDataStream&, int, int) const;

template<typename Stream>
void CMasternodeVote::Unserialize(Stream& s, int nType, int nVersion)
{
	SerializationOp(s, CSerActionUnserialize(), nType, nVersion);
}

template void CMasternodeVote::Unserialize<CDataStream>(CDataStream&, int, int);

template <typename Stream, typename Operation>
inline void CMasternodeVote::SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
{
	unsigned int nSerSize = 0;

	READWRITE(voterVin);
	READWRITE(nBlockHeight);
	READWRITE(payeeScript);
	READWRITE(nTimeSigned);
	READWRITE(vchSig);
}
