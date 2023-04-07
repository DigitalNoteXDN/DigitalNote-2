#ifndef CKEYMETADATA_H
#define CKEYMETADATA_H

#include <cstdint>

class CKeyMetadata
{
public:
	static const int CURRENT_VERSION=1;
	int nVersion;
	int64_t nCreateTime; // 0 means unknown

	CKeyMetadata();
	CKeyMetadata(int64_t nCreateTime_);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream &s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream &s, int nType, int nVersion);

	void SetNull();
};

#endif // CKEYMETADATA_H
