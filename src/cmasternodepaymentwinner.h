#ifndef CMASTERNODEPAYMENTWINNER_H
#define CMASTERNODEPAYMENTWINNER_H

#include <vector>

#include "ctxin.h"
#include "cscript.h"

class uint256;

// for storing the winning payments
class CMasternodePaymentWinner
{
public:
	int nBlockHeight;
	CTxIn vin;
	CScript payee;
	std::vector<unsigned char> vchSig;
	uint64_t score;

	CMasternodePaymentWinner();

	uint256 GetHash();

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CMASTERNODEPAYMENTWINNER_H
