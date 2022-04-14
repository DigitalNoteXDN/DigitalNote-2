#ifndef CTXINDEX_H
#define CTXINDEX_H

#include <vector>

#include "cdisktxpos.h"

class CTxIndex;

bool operator==(const CTxIndex& a, const CTxIndex& b);
bool operator!=(const CTxIndex& a, const CTxIndex& b);

/**  A txdb record that contains the disk location of a transaction and the
 * locations of transactions that spend its outputs.  vSpent is really only
 * used as a flag, but having the location is very helpful for debugging.
 */
class CTxIndex
{
public:
	CDiskTxPos pos;
	std::vector<CDiskTxPos> vSpent;

	CTxIndex();
	CTxIndex(const CDiskTxPos& posIn, unsigned int nOutputs);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	void SetNull();
	bool IsNull();
	friend bool operator==(const CTxIndex& a, const CTxIndex& b);
	friend bool operator!=(const CTxIndex& a, const CTxIndex& b);
	int GetDepthInMainChain() const;
};

#endif // CTXINDEX_H
