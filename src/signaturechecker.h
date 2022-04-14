#ifndef SIGNATURECHECK_H
#define SIGNATURECHECK_H

#include <vector>

class CTransaction;
class CScript;
class CPubKey;
class uint256;

class BaseSignatureChecker
{
public:
	virtual bool CheckSig(const std::vector<unsigned char>& scriptSig, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode) const
	{
		return false;
	}

	virtual ~BaseSignatureChecker()
	{
		
	}
};

class SignatureChecker : public BaseSignatureChecker
{
private:
	const CTransaction& txTo;
	unsigned int nIn;

protected:
	virtual bool VerifySignature(const std::vector<unsigned char>& vchSig, const CPubKey& vchPubKey, const uint256& sighash) const;

public:
	SignatureChecker(const CTransaction& txToIn, unsigned int nInIn);
	bool CheckSig(const std::vector<unsigned char>& scriptSig, const std::vector<unsigned char>& vchPubKey,
			const CScript& scriptCode) const;
};

#endif // SIGNATURECHECK_H