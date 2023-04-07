#ifndef CTXOUT_H
#define CTXOUT_H

#include "cscript.h"

class uint256;
class CTxOut;

bool operator==(const CTxOut& a, const CTxOut& b);
bool operator!=(const CTxOut& a, const CTxOut& b);

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
	int64_t nValue;
	int nRounds;
	CScript scriptPubKey;

	CTxOut();
	CTxOut(int64_t nValueIn, CScript scriptPubKeyIn);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	void SetNull();
	bool IsNull() const;
	void SetEmpty();
	bool IsEmpty() const;
	uint256 GetHash() const;

	bool IsDust(int64_t MIN_RELAY_TX_FEE) const;
	friend bool operator==(const CTxOut& a, const CTxOut& b);
	friend bool operator!=(const CTxOut& a, const CTxOut& b);
	//CTxOut& operator=(const CTxOut&) {};

	std::string ToString() const;
};

#endif // CTXOUT_H
