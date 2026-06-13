#include "compat.h"

#include <boost/lexical_cast.hpp>

#include "util.h"
#include "hash.h"
#include "ckey.h"
#include "cpubkey.h"
#include "cdatastream.h"
#include "cmnenginesigner.h"
#include "mnengine_extern.h"

#include "cmasternodevotequeue.h"

/*
 * Implementation references:
 *
 * - v208-M1Q-queue-based-voting-SPEC.md S4 specifies the wire format.
 *   The canonical signable form is
 *     voterVin || nQueueHeight || concat(payeeScript.ToString() for each
 *                                         entry in vPayeeQueue, in order) ||
 *     nTimeSigned
 *   Each component is formatted the same way the legacy CMasternodeVote
 *   does -- string concatenation via boost::lexical_cast and component
 *   ToString().
 *
 * - The order of payees in the concatenation IS the queue order; any
 *   reordering by an attacker would change the signable string and
 *   invalidate the signature.  Order is binding.
 *
 * - Signing uses mnEngineSigner.{SetKey, SignMessage, VerifyMessage} --
 *   the same primitives as dsee/dseep/mnw/mnvote.  Reuses the existing
 *   masternodeprivkey infrastructure; no new keys.
 */

CMasternodeVoteQueue::CMasternodeVoteQueue()
	: nQueueHeight(0), nTimeSigned(0)
{
}

CMasternodeVoteQueue::CMasternodeVoteQueue(const CTxIn &vinIn, int nQueueHeightIn,
                                           const std::vector<CScript> &vPayeeQueueIn)
	: voterVin(vinIn),
	  nQueueHeight(nQueueHeightIn),
	  vPayeeQueue(vPayeeQueueIn),
	  nTimeSigned(0)
{
}

std::string CMasternodeVoteQueue::GetSignableString() const
{
	// Canonical signable representation.  Concatenate each payee in order;
	// the order itself is part of the signed content (a queue is an ordered
	// list, and reordering must invalidate the signature).
	std::string strMessage =
		voterVin.ToString() +
		boost::lexical_cast<std::string>(nQueueHeight);

	for (std::vector<CScript>::const_iterator it = vPayeeQueue.begin();
	     it != vPayeeQueue.end(); ++it)
	{
		strMessage += it->ToString();
	}

	strMessage += boost::lexical_cast<std::string>(nTimeSigned);

	return strMessage;
}

uint256 CMasternodeVoteQueue::GetHash() const
{
	// Inv-mechanism hash.  Combines all signable fields so that two queues
	// from the same voter at the same nQueueHeight with different content
	// (the equivocation case) hash distinctly -- both copies travel the
	// network and the receiver sees the equivocation rather than treating
	// the second as a duplicate.
	//
	// Each payee is streamed individually rather than streaming the whole
	// vPayeeQueue vector: CScript's stream operator<< is already
	// instantiated project-wide, but CDataStream::operator<< for a
	// std::vector<CScript> is not (no other code streams a bare
	// vector<CScript> member).  Streaming element-by-element uses only the
	// scalar CScript operator and avoids needing a new instantiation, while
	// producing an equally unique fingerprint.  The queue length is fixed
	// (VOTE_QUEUE_LENGTH), so element-wise streaming is unambiguous.
	CDataStream ss(SER_GETHASH, 0);

	ss << voterVin;
	ss << nQueueHeight;
	for (std::vector<CScript>::const_iterator it = vPayeeQueue.begin();
	     it != vPayeeQueue.end(); ++it)
	{
		ss << *it;
	}
	ss << nTimeSigned;

	return Hash(ss.begin(), ss.end());
}

bool CMasternodeVoteQueue::Sign(const std::string &strMnPrivKey)
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
		LogPrintf("CMasternodeVoteQueue::Sign -- SetKey failed: %s\n", errorMessage.c_str());

		return false;
	}

	std::string strMessage = GetSignableString();

	if (!mnEngineSigner.SignMessage(strMessage, errorMessage, vchSig, key))
	{
		LogPrintf("CMasternodeVoteQueue::Sign -- SignMessage failed: %s\n", errorMessage.c_str());

		return false;
	}

	// Self-verify guard.  Same pattern as CMasternodePayments::Sign and
	// CMasternodeVote::Sign -- catches signature corruption immediately
	// rather than letting an invalid queue travel the network.
	if (!mnEngineSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage))
	{
		LogPrintf("CMasternodeVoteQueue::Sign -- self-verify failed: %s\n", errorMessage.c_str());

		return false;
	}

	return true;
}

bool CMasternodeVoteQueue::CheckSignature(const CPubKey &voterPubKey) const
{
	std::string strMessage = GetSignableString();
	std::string errorMessage;

	// The project's CMNengineSigner::VerifyMessage takes vchSig by non-const
	// reference (legacy API).  We're const, so make a copy.  Cost is trivial
	// (vchSig is ~65 bytes for an ECDSA signature).
	std::vector<unsigned char> vchSigCopy = vchSig;

	if (!mnEngineSigner.VerifyMessage(voterPubKey, vchSigCopy, strMessage, errorMessage))
	{
		LogPrintf("CMasternodeVoteQueue::CheckSignature -- verify failed for voter %s "
		          "nQueueHeight %d: %s\n",
		          voterVin.prevout.ToString(), nQueueHeight, errorMessage.c_str());

		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Serialization (standard project pattern, mirrors CMasternodeVote minus the
// unused GetSerializeSize variant -- see header comment).
// ---------------------------------------------------------------------------

template<typename Stream>
void CMasternodeVoteQueue::Serialize(Stream& s, int nType, int nVersion) const
{
	NCONST_PTR(this)->SerializationOp(s, CSerActionSerialize(), nType, nVersion);
}

template void CMasternodeVoteQueue::Serialize<CDataStream>(CDataStream&, int, int) const;

template<typename Stream>
void CMasternodeVoteQueue::Unserialize(Stream& s, int nType, int nVersion)
{
	SerializationOp(s, CSerActionUnserialize(), nType, nVersion);
}

template void CMasternodeVoteQueue::Unserialize<CDataStream>(CDataStream&, int, int);

template <typename Stream, typename Operation>
inline void CMasternodeVoteQueue::SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
{
	unsigned int nSerSize = 0;

	READWRITE(voterVin);
	READWRITE(nQueueHeight);
	READWRITE(vPayeeQueue);
	READWRITE(nTimeSigned);
	READWRITE(vchSig);
}
