#ifndef CDISKBLOCKPOS_H
#define CDISKBLOCKPOS_H

#include <string>

class CDiskBlockPos;

bool operator==(const CDiskBlockPos &a, const CDiskBlockPos &b);
bool operator!=(const CDiskBlockPos &a, const CDiskBlockPos &b);

// Adaptive block sizing depends on this
struct CDiskBlockPos
{
	int nFile;
	unsigned int nPos;

	CDiskBlockPos();
	CDiskBlockPos(int nFileIn, unsigned int nPosIn);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	friend bool operator==(const CDiskBlockPos &a, const CDiskBlockPos &b);
	friend bool operator!=(const CDiskBlockPos &a, const CDiskBlockPos &b);
	void SetNull();
	bool IsNull() const;

	std::string ToString() const;
};

#endif // CDISKBLOCKPOS_H
