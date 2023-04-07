#ifndef CBANENTRY_H
#define CBANENTRY_H

#include <string>

class CBanEntry
{
public:
	static const int CURRENT_VERSION=1;
	int nVersion;
	int64_t nCreateTime;
	int64_t nBanUntil;
	uint8_t banReason;

	CBanEntry();
	CBanEntry(int64_t nCreateTimeIn);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	void SetNull();
	std::string banReasonToString();
};

#endif // CBANENTRY_H
