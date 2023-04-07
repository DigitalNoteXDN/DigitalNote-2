#ifndef CDISKTXPOS_H
#define CDISKTXPOS_H

#include <string>

class CDiskTxPos;

bool operator==(const CDiskTxPos& a, const CDiskTxPos& b);
bool operator!=(const CDiskTxPos& a, const CDiskTxPos& b);

/** Position on disk for a particular transaction. */
class CDiskTxPos
{
public:
	unsigned int nFile;
	unsigned int nBlockPos;
	unsigned int nTxPos;

	CDiskTxPos();
	CDiskTxPos(unsigned int nFileIn, unsigned int nBlockPosIn, unsigned int nTxPosIn);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	void SetNull();
	bool IsNull() const;
	friend bool operator==(const CDiskTxPos& a, const CDiskTxPos& b);
	friend bool operator!=(const CDiskTxPos& a, const CDiskTxPos& b);
	std::string ToString() const;
};

#endif // CDISKTXPOS_H
