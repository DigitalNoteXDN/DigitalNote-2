#include "cscript.h"
#include "cpubkey.h"
#include "uint/uint256.h"
#include "script.h"

#include "signaturechecker.h"

SignatureChecker::SignatureChecker(const CTransaction& txToIn, unsigned int nInIn) : txTo(txToIn), nIn(nInIn) {
	
}

bool SignatureChecker::VerifySignature(const std::vector<unsigned char>& vchSig, const CPubKey& pubkey, const uint256& sighash) const
{
	return pubkey.Verify(sighash, vchSig);
}

bool SignatureChecker::CheckSig(const std::vector<unsigned char>& vchSigIn, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode) const
{
	CPubKey pubkey(vchPubKey);

	if (!pubkey.IsValid())
	{
		return false;
	}

	// Hash type is one byte tacked on to the end of the signature
	std::vector<unsigned char> vchSig(vchSigIn);
	
	if (vchSig.empty())
	{
		return false;
	}
	
	int nHashType = vchSig.back();
	vchSig.pop_back();

	uint256 sighash = SignatureHash(scriptCode, txTo, nIn, nHashType);

	if (!VerifySignature(vchSig, pubkey, sighash))
	{
		return false;
	}
	
	return true;
}

