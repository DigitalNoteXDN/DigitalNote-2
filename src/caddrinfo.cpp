#include "compat.h"

#include <cmath>

#include "enums/serialize_type.h"
#include "crypto/bmw/bmw512.h"
#include "addrman.h"
#include "cdatastream.h"
#include "types/cserializedata.h"
#include "serialize.h"

#include "caddrinfo.h"

CAddrInfo::CAddrInfo(const CAddress &addrIn, const CNetAddr &addrSource) : CAddress(addrIn), source(addrSource)
{
	Init();
}

CAddrInfo::CAddrInfo() : CAddress(), source()
{
	Init();
}

void CAddrInfo::Init()
{
	nLastSuccess = 0;
	nLastTry = 0;
	nAttempts = 0;
	nRefCount = 0;
	fInTried = false;
	nRandomPos = -1;
}

int CAddrInfo::GetTriedBucket(const std::vector<unsigned char> &nKey) const
{
    CDataStream ss1(SER_GETHASH, 0);
    CDataStream ss2(SER_GETHASH, 0);
    std::vector<unsigned char> vchKey = GetKey();
	std::vector<unsigned char> vchGroupKey = GetGroup();
    
	ss1 << nKey << vchKey;
    
	uint64_t hash1 = Hash_bmw512(ss1.begin(), ss1.end()).Get64();

    ss2 << nKey << vchGroupKey << (hash1 % ADDRMAN_TRIED_BUCKETS_PER_GROUP);
    
	uint64_t hash2 = Hash_bmw512(ss2.begin(), ss2.end()).Get64();
    
	return hash2 % ADDRMAN_TRIED_BUCKET_COUNT;
}

int CAddrInfo::GetNewBucket(const std::vector<unsigned char> &nKey, const CNetAddr& src) const
{
    CDataStream ss1(SER_GETHASH, 0);
    CDataStream ss2(SER_GETHASH, 0);
    std::vector<unsigned char> vchGroupKey = GetGroup();
    std::vector<unsigned char> vchSourceGroupKey = src.GetGroup();
    
	ss1 << nKey << vchGroupKey << vchSourceGroupKey;
    
	uint64_t hash1 = Hash_bmw512(ss1.begin(), ss1.end()).Get64();
	
	ss2 << nKey << vchSourceGroupKey << (hash1 % ADDRMAN_NEW_BUCKETS_PER_SOURCE_GROUP);
    
	uint64_t hash2 = Hash_bmw512(ss2.begin(), ss2.end()).Get64();
    
	return hash2 % ADDRMAN_NEW_BUCKET_COUNT;
}

int CAddrInfo::GetNewBucket(const std::vector<unsigned char> &nKey) const
{
	return GetNewBucket(nKey, source);
}

bool CAddrInfo::IsTerrible(int64_t nNow) const
{
    if (nLastTry && nLastTry >= nNow-60) // never remove things tried the last minute
	{
        return false;
	}
	
    if (nTime > nNow + 10*60) // came in a flying DeLorean
	{
        return true;
	}
	
    if (nTime==0 || nNow-nTime > ADDRMAN_HORIZON_DAYS*86400) // not seen in over a month
	{
        return true;
	}
	
    if (nLastSuccess==0 && nAttempts>=ADDRMAN_RETRIES) // tried three times and never a success
	{
        return true;
	}
	
    if (nNow-nLastSuccess > ADDRMAN_MIN_FAIL_DAYS*86400 && nAttempts>=ADDRMAN_MAX_FAILURES) // 10 successive failures in the last week
	{
        return true;
	}

    return false;
}

double CAddrInfo::GetChance(int64_t nNow) const
{
    double fChance = 1.0;

    int64_t nSinceLastSeen = nNow - nTime;
    int64_t nSinceLastTry = nNow - nLastTry;

    if (nSinceLastSeen < 0)
	{
		nSinceLastSeen = 0;
	}
	
    if (nSinceLastTry < 0)
	{
		nSinceLastTry = 0;
	}
	
    fChance *= 600.0 / (600.0 + nSinceLastSeen);

    // deprioritize very recent attempts away
    if (nSinceLastTry < 60*10)
	{
        fChance *= 0.01;
	}
	
    // deprioritize 66% after each failed attempt, but at most 1/28th to avoid the search taking forever or overly penalizing outages.
    fChance *= pow(0.66, std::min(nAttempts, 8));

    return fChance;
}

unsigned int CAddrInfo::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	const bool fGetSize = true;
	const bool fWrite = false;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	s.nType = nType;
	s.nVersion = nVersion;
	
	CAddress* pthis = (CAddress*)(this);
	
	READWRITE(*pthis);
	READWRITE(source);
	READWRITE(nLastSuccess);
	READWRITE(nAttempts);
	
	return nSerSize;
}

template<typename Stream>
void CAddrInfo::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	CAddress* pthis = (CAddress*)(this);
	
	READWRITE(*pthis);
	READWRITE(source);
	READWRITE(nLastSuccess);
	READWRITE(nAttempts);
}

template<typename Stream>
void CAddrInfo::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	CAddress* pthis = (CAddress*)(this);
	
	READWRITE(*pthis);
	READWRITE(source);
	READWRITE(nLastSuccess);
	READWRITE(nAttempts);
}

template void CAddrInfo::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CAddrInfo::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

