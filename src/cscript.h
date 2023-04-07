#ifndef CSCRIPT_H
#define CSCRIPT_H

#include <vector>

#include "types/ctxdestination.h"
#include "enums/opcodetype.h"

class uint160;
class uint256;
class CBigNum;
class CPubKey;

/** Serialized script, used inside transaction inputs and outputs */
class CScript : public std::vector<unsigned char>
{
protected:
	CScript& push_int64(int64_t n);
	CScript& push_uint64(uint64_t n);

public:
	CScript();
	CScript(const CScript& b);
	CScript(const_iterator pbegin, const_iterator pend);
#ifndef _MSC_VER
	CScript(const unsigned char* pbegin, const unsigned char* pend);
#endif

	CScript& operator+=(const CScript& b);
	friend CScript operator+(const CScript& a, const CScript& b);

	//
	// Issue: implicitly-declared '[...]::operator=([...])' is deprecated.
	// Fix: https://github.com/tsduck/tsduck/issues/205
	//
	CScript& operator=(const CScript& other) = default;

	//explicit CScript(char b) is not portable.  Use 'signed char' or 'unsigned char'.
	explicit CScript(signed char b);
	explicit CScript(short b);
	explicit CScript(int b);
	explicit CScript(long b);
	explicit CScript(long long b);
	explicit CScript(unsigned char b);
	explicit CScript(unsigned int b);
	explicit CScript(unsigned short b);
	explicit CScript(unsigned long b);
	explicit CScript(unsigned long long b);

	explicit CScript(opcodetype b);
	explicit CScript(const uint256& b);
	explicit CScript(const CBigNum& b);
	explicit CScript(const std::vector<unsigned char>& b);

	//CScript& operator<<(char b) is not portable.  Use 'signed char' or 'unsigned char'.
	CScript& operator<<(signed char b);
	CScript& operator<<(short b);
	CScript& operator<<(int b);
	CScript& operator<<(long b);
	CScript& operator<<(long long b);
	CScript& operator<<(unsigned char b);
	CScript& operator<<(unsigned int b);
	CScript& operator<<(unsigned short b);
	CScript& operator<<(unsigned long b);
	CScript& operator<<(unsigned long long b);
	CScript& operator<<(opcodetype opcode);
	CScript& operator<<(const uint160& b);
	CScript& operator<<(const uint256& b);
	CScript& operator<<(const CPubKey& key);
	CScript& operator<<(const CBigNum& b);
	CScript& operator<<(const std::vector<unsigned char>& b);
	CScript& operator<<(const CScript& b);

	bool GetOp(iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>& vchRet);
	bool GetOp(iterator& pc, opcodetype& opcodeRet);
	bool GetOp(const_iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>& vchRet) const;
	bool GetOp(const_iterator& pc, opcodetype& opcodeRet) const;
	bool GetOp2(const_iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>* pvchRet) const;

	// Encode/decode small integers:
	static int DecodeOP_N(opcodetype opcode);
	static opcodetype EncodeOP_N(int n);
	int FindAndDelete(const CScript& b);
	int Find(opcodetype op) const;

	// Pre-version-0.6, Bitcoin always counted CHECKMULTISIGs
	// as 20 sigops. With pay-to-script-hash, that changed:
	// CHECKMULTISIGs serialized in scriptSigs are
	// counted more accurately, assuming they are of the form
	//  ... OP_N CHECKMULTISIG ...
	unsigned int GetSigOpCount(bool fAccurate) const;

	// Accurately count sigOps, including sigOps in
	// pay-to-script-hash transactions:
	unsigned int GetSigOpCount(const CScript& scriptSig) const;
	bool IsNormalPaymentScript() const;
	bool IsPayToScriptHash() const;

	// Called by IsStandardTx and P2SH VerifyScript (which makes it consensus-critical).
	bool IsPushOnly() const;
	/**
	* Returns whether the script is guaranteed to fail at execution,
	* regardless of the initial stack. This allows outputs to be pruned
	* instantly when entering the UTXO set.
	*/
	bool IsUnspendable() const;
	// Called by IsStandardTx.
	bool HasCanonicalPushes() const;
	void SetDestination(const CTxDestination& address);
	void SetMultisig(int nRequired, const std::vector<CPubKey>& keys);
	std::string ToString(bool fShort=false) const;
	CScriptID GetID() const;
	void clear();
};

#endif // CSCRIPT_H
