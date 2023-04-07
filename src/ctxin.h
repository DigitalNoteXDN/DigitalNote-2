#ifndef CTXIN_H
#define CTXIN_H

#include <limits>

#include "coutpoint.h"
#include "cscript.h"

class CTxIn;

bool operator!=(const CTxIn& a, const CTxIn& b);
bool operator==(const CTxIn& a, const CTxIn& b);

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
	COutPoint prevout;
	CScript scriptSig;
	CScript prevPubKey;
	unsigned int nSequence;

	CTxIn();
	explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn=CScript(),
			unsigned int nSequenceIn=std::numeric_limits<unsigned int>::max());
	explicit CTxIn(uint256 hashPrevTx, unsigned int nOut, CScript scriptSigIn=CScript(),
			unsigned int nSequenceIn=std::numeric_limits<unsigned int>::max());

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	bool IsFinal() const;
	friend bool operator==(const CTxIn& a, const CTxIn& b);
	friend bool operator!=(const CTxIn& a, const CTxIn& b);
	std::string ToString() const;
};

#endif // CTXIN_H
