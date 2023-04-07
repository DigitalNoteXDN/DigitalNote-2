#ifndef CDISKBLOCKINDEX_H
#define CDISKBLOCKINDEX_H

#include <string>

#include "cblockindex.h"
#include "uint/uint256.h"

/** Used to marshal pointers into hashes for db storage. */
class CDiskBlockIndex : public CBlockIndex
{
private:
	uint256 blockHash;

public:
	uint256 hashPrev;
	uint256 hashNext;

	CDiskBlockIndex();
	explicit CDiskBlockIndex(CBlockIndex* pindex);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	uint256 GetBlockHash() const;
	std::string ToString() const;
};

#endif // CDISKBLOCKINDEX_H
