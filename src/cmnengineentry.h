#ifndef CMNENFINEENTRY_H
#define CMNENFINEENTRY_H

#include <vector>

#include "ctransaction.h"

class CTxIn;
class CTxOut;
class CTxDSIn;
class CTxDSOut;

// A clients transaction in the mnengine pool
class CMNengineEntry
{
public:
	bool isSet;
	std::vector<CTxDSIn> sev;
	std::vector<CTxDSOut> vout;
	int64_t amount;
	CTransaction collateral;
	CTransaction txSupporting;
	int64_t addedTime;							// time in UTC milliseconds

	CMNengineEntry();

	/// Add entries to use for MNengine
	bool Add(const std::vector<CTxIn> vinIn, int64_t amountIn, const CTransaction collateralIn, const std::vector<CTxOut> voutIn);
	bool AddSig(const CTxIn& vin);
	bool IsExpired();
};

#endif // CMNENFINEENTRY_H
