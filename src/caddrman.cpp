#include "compat.h"

#include "caddrinfo.h"
#include "addrman.h"
#include "thread.h"
#include "cdatastream.h"
#include "serialize.h"

#include "caddrman.h"

CAddrInfo* CAddrMan::Find(const CNetAddr& addr, int *pnId)
{
	std::map<CNetAddr, int>::iterator it = mapAddr.find(addr);

	if (it == mapAddr.end())
	{
		return NULL;
	}

	if (pnId)
	{
		*pnId = (*it).second;
	}

	std::map<int, CAddrInfo>::iterator it2 = mapInfo.find((*it).second);

	if (it2 != mapInfo.end())
	{
		return &(*it2).second;
	}

	return NULL;
}

CAddrInfo* CAddrMan::Create(const CAddress &addr, const CNetAddr &addrSource, int *pnId)
{
	int nId = nIdCount++;

	mapInfo[nId] = CAddrInfo(addr, addrSource);
	mapAddr[addr] = nId;
	mapInfo[nId].nRandomPos = vRandom.size();

	vRandom.push_back(nId);

	if (pnId)
	{
		*pnId = nId;
	}

	return &mapInfo[nId];
}

void CAddrMan::SwapRandom(unsigned int nRndPos1, unsigned int nRndPos2)
{
	if (nRndPos1 == nRndPos2)
	{
		return;
	}

	assert(nRndPos1 < vRandom.size() && nRndPos2 < vRandom.size());

	int nId1 = vRandom[nRndPos1];
	int nId2 = vRandom[nRndPos2];

	assert(mapInfo.count(nId1) == 1);
	assert(mapInfo.count(nId2) == 1);

	mapInfo[nId1].nRandomPos = nRndPos2;
	mapInfo[nId2].nRandomPos = nRndPos1;

	vRandom[nRndPos1] = nId2;
	vRandom[nRndPos2] = nId1;
}

int CAddrMan::SelectTried(int nKBucket)
{
	std::vector<int> &vTried = vvTried[nKBucket];

	// random shuffle the first few elements (using the entire list)
	// find the least recently tried among them
	int64_t nOldest = -1;
	int nOldestPos = -1;

	for (unsigned int i = 0; i < ADDRMAN_TRIED_ENTRIES_INSPECT_ON_EVICT && i < vTried.size(); i++)
	{
		int nPos = GetRandInt(vTried.size() - i) + i;
		int nTemp = vTried[nPos];
		
		vTried[nPos] = vTried[i];
		vTried[i] = nTemp;
		
		assert(nOldest == -1 || mapInfo.count(nTemp) == 1);
		
		if (nOldest == -1 || mapInfo[nTemp].nLastSuccess < mapInfo[nOldest].nLastSuccess)
		{
		   nOldest = nTemp;
		   nOldestPos = nPos;
		}
	}

	return nOldestPos;
}

int CAddrMan::ShrinkNew(int nUBucket)
{
	assert(nUBucket >= 0 && (unsigned int)nUBucket < vvNew.size());

	std::set<int> &vNew = vvNew[nUBucket];

	// first look for deletable items
	for (std::set<int>::iterator it = vNew.begin(); it != vNew.end(); it++)
	{
		assert(mapInfo.count(*it));
		
		CAddrInfo &info = mapInfo[*it];
		
		if (info.IsTerrible())
		{
			if (--info.nRefCount == 0)
			{
				SwapRandom(info.nRandomPos, vRandom.size()-1);
				
				vRandom.pop_back();
				mapAddr.erase(info);
				mapInfo.erase(*it);
				
				nNew--;
			}
			
			vNew.erase(it);
			
			return 0;
		}
	}

	// otherwise, select four randomly, and pick the oldest of those to replace
	int n[4] = {GetRandInt(vNew.size()), GetRandInt(vNew.size()), GetRandInt(vNew.size()), GetRandInt(vNew.size())};
	int nI = 0;
	int nOldest = -1;

	for (std::set<int>::iterator it = vNew.begin(); it != vNew.end(); it++)
	{
		if (nI == n[0] || nI == n[1] || nI == n[2] || nI == n[3])
		{
			assert(nOldest == -1 || mapInfo.count(*it) == 1);
			
			if (nOldest == -1 || mapInfo[*it].nTime < mapInfo[nOldest].nTime)
			{
				nOldest = *it;
			}
		}
		
		nI++;
	}

	assert(mapInfo.count(nOldest) == 1);

	CAddrInfo &info = mapInfo[nOldest];

	if (--info.nRefCount == 0)
	{
		SwapRandom(info.nRandomPos, vRandom.size()-1);
		
		vRandom.pop_back();
		mapAddr.erase(info);
		mapInfo.erase(nOldest);
		
		nNew--;
	}

	vNew.erase(nOldest);

	return 1;
}

void CAddrMan::MakeTried(CAddrInfo& info, int nId, int nOrigin)
{
	assert(vvNew[nOrigin].count(nId) == 1);

	// remove the entry from all new buckets
	for (std::vector<std::set<int> >::iterator it = vvNew.begin(); it != vvNew.end(); it++)
	{
		if ((*it).erase(nId))
		{
			info.nRefCount--;
		}
	}

	nNew--;

	assert(info.nRefCount == 0);

	// what tried bucket to move the entry to
	int nKBucket = info.GetTriedBucket(nKey);
	std::vector<int> &vTried = vvTried[nKBucket];

	// first check whether there is place to just add it
	if (vTried.size() < ADDRMAN_TRIED_BUCKET_SIZE)
	{
		vTried.push_back(nId);
		nTried++;
		info.fInTried = true;
		
		return;
	}

	// otherwise, find an item to evict
	int nPos = SelectTried(nKBucket);

	// find which new bucket it belongs to
	assert(mapInfo.count(vTried[nPos]) == 1);

	int nUBucket = mapInfo[vTried[nPos]].GetNewBucket(nKey);
	std::set<int> &vNew = vvNew[nUBucket];

	// remove the to-be-replaced tried entry from the tried set
	CAddrInfo& infoOld = mapInfo[vTried[nPos]];
	infoOld.fInTried = false;
	infoOld.nRefCount = 1;
	// do not update nTried, as we are going to move something else there immediately

	// check whether there is place in that one,
	if (vNew.size() < ADDRMAN_NEW_BUCKET_SIZE)
	{   // if so, move it back there
		vNew.insert(vTried[nPos]);
	}
	else
	{   // otherwise, move it to the new bucket nId came from (there is certainly place there)
		vvNew[nOrigin].insert(vTried[nPos]);
	}
	nNew++;

	vTried[nPos] = nId;
	// we just overwrote an entry in vTried; no need to update nTried
	info.fInTried = true;

	return;
}

void CAddrMan::Good_(const CService &addr, int64_t nTime)
{
	int nId;
	CAddrInfo *pinfo = Find(addr, &nId);

	// if not found, bail out
	if (!pinfo)
	{
		return;
	}

	CAddrInfo &info = *pinfo;

	// check whether we are talking about the exact same CService (including same port)
	if (info != addr)
	{
		return;
	}

	// update info
	info.nLastSuccess = nTime;
	info.nLastTry = nTime;
	info.nTime = nTime;
	info.nAttempts = 0;

	// if it is already in the tried set, don't do anything else
	if (info.fInTried)
	{
		return;
	}

	// find a bucket it is in now
	int nRnd = GetRandInt(vvNew.size());
	int nUBucket = -1;

	for (unsigned int n = 0; n < vvNew.size(); n++)
	{
		int nB = (n+nRnd) % vvNew.size();
		std::set<int> &vNew = vvNew[nB];
		
		if (vNew.count(nId))
		{
			nUBucket = nB;
			
			break;
		}
	}

	// if no bucket is found, something bad happened;
	// TODO: maybe re-add the node, but for now, just bail out
	if (nUBucket == -1)
	{
		return;
	}

	LogPrint("addrman", "Moving %s to tried\n", addr.ToString());

	// move nId to the tried tables
	MakeTried(info, nId, nUBucket);
}

bool CAddrMan::Add_(const CAddress &addr, const CNetAddr& source, int64_t nTimePenalty)
{
	if (!addr.IsRoutable())
	{
		return false;
	}

	if (addr.GetPort() == 0)
	{
		return false;
	}

	bool fNew = false;
	int nId;
	CAddrInfo *pinfo = Find(addr, &nId);

	if (pinfo)
	{
		// periodically update nTime
		bool fCurrentlyOnline = (GetAdjustedTime() - addr.nTime < 24 * 60 * 60);
		int64_t nUpdateInterval = (fCurrentlyOnline ? 60 * 60 : 24 * 60 * 60);
		
		if (addr.nTime && (!pinfo->nTime || pinfo->nTime < addr.nTime - nUpdateInterval - nTimePenalty))
		{
			pinfo->nTime = std::max((int64_t)0, addr.nTime - nTimePenalty);
		}
		
		// add services
		pinfo->nServices |= addr.nServices;

		// do not update if no new information is present
		if (!addr.nTime || (pinfo->nTime && addr.nTime <= pinfo->nTime))
		{
			return false;
		}
		
		// do not update if the entry was already in the "tried" table
		if (pinfo->fInTried)
		{
			return false;
		}
		
		// do not update if the max reference count is reached
		if (pinfo->nRefCount == ADDRMAN_NEW_BUCKETS_PER_ADDRESS)
		{
			return false;
		}
		
		// stochastic test: previous nRefCount == N: 2^N times harder to increase it
		int nFactor = 1;
		
		for (int n = 0; n < pinfo->nRefCount; n++)
		{
			nFactor *= 2;
		}
		
		if (nFactor > 1 && (GetRandInt(nFactor) != 0))
		{
			return false;
		}
	}
	else
	{
		pinfo = Create(addr, source, &nId);
		pinfo->nTime = std::max((int64_t)0, (int64_t)pinfo->nTime - nTimePenalty);
		nNew++;
		fNew = true;
	}

	int nUBucket = pinfo->GetNewBucket(nKey, source);
	std::set<int> &vNew = vvNew[nUBucket];

	if (!vNew.count(nId))
	{
		pinfo->nRefCount++;
		
		if (vNew.size() == ADDRMAN_NEW_BUCKET_SIZE)
		{
			ShrinkNew(nUBucket);
		}
		
		vvNew[nUBucket].insert(nId);
	}

	return fNew;
}

void CAddrMan::Attempt_(const CService &addr, int64_t nTime)
{
	CAddrInfo *pinfo = Find(addr);

	// if not found, bail out
	if (!pinfo)
	{
		return;
	}

	CAddrInfo &info = *pinfo;

	// check whether we are talking about the exact same CService (including same port)
	if (info != addr)
	{
		return;
	}

	// update info
	info.nLastTry = nTime;
	info.nAttempts++;
}

CAddress CAddrMan::Select_(int nUnkBias)
{
	if (size() == 0)
	{
		return CAddress();
	}

	double nCorTried = sqrt(nTried) * (100.0 - nUnkBias);
	double nCorNew = sqrt(nNew) * nUnkBias;

	if ((nCorTried + nCorNew) * GetRandInt(1 << 30) / (1 << 30) < nCorTried)
	{
		// use a tried node
		double fChanceFactor = 1.0;
		
		while(1)
		{
			int nKBucket = GetRandInt(vvTried.size());
			
			std::vector<int> &vTried = vvTried[nKBucket];
			
			if (vTried.size() == 0)
			{
				continue;
			}
			
			int nPos = GetRandInt(vTried.size());
			
			assert(mapInfo.count(vTried[nPos]) == 1);
			
			CAddrInfo &info = mapInfo[vTried[nPos]];
			
			if (GetRandInt(1 << 30) < fChanceFactor * info.GetChance() * (1 << 30))
			{
				return info;
			}
			
			fChanceFactor *= 1.2;
		}
	}
	else
	{
		// use a new node
		double fChanceFactor = 1.0;
		
		while(1)
		{
			int nUBucket = GetRandInt(vvNew.size());
			std::set<int> &vNew = vvNew[nUBucket];
			
			if (vNew.size() == 0)
			{
				continue;
			}
			
			int nPos = GetRandInt(vNew.size());
			
			std::set<int>::iterator it = vNew.begin();
			
			while (nPos--)
			{
				it++;
			}
			
			assert(mapInfo.count(*it) == 1);
			
			CAddrInfo &info = mapInfo[*it];
			if (GetRandInt(1<<30) < fChanceFactor*info.GetChance()*(1<<30))
			{
				return info;
			}
			
			fChanceFactor *= 1.2;
		}
	}
}

#ifdef DEBUG_ADDRMAN
int CAddrMan::Check_()
{
	std::set<int> setTried;
	std::map<int, int> mapNew;

	if (vRandom.size() != nTried + nNew)
	{
		return -7;
	}

	for (std::map<int, CAddrInfo>::iterator it = mapInfo.begin(); it != mapInfo.end(); it++)
	{
		int n = (*it).first;
		CAddrInfo &info = (*it).second;
		
		if (info.fInTried)
		{

			if (!info.nLastSuccess)
			{
				return -1;
			}
			
			if (info.nRefCount)
			{
				return -2;
			}
			
			setTried.insert(n);
		}
		else
		{
			if (info.nRefCount < 0 || info.nRefCount > ADDRMAN_NEW_BUCKETS_PER_ADDRESS)
			{
				return -3;
			}
			
			if (!info.nRefCount)
			{
				return -4;
			}
			
			mapNew[n] = info.nRefCount;
		}
		
		if (mapAddr[info] != n)
		{
			return -5;
		}
		
		if (info.nRandomPos<0 || info.nRandomPos>=vRandom.size() || vRandom[info.nRandomPos] != n)
		{
			return -14;
		}
		
		if (info.nLastTry < 0)
		{
			return -6;
		}
		
		if (info.nLastSuccess < 0)
		{
			return -8;
		}
	}

	if (setTried.size() != nTried)
	{
		return -9;
	}

	if (mapNew.size() != nNew)
	{
		return -10;
	}

	for (int n = 0; n < vvTried.size(); n++)
	{
		std::vector<int> &vTried = vvTried[n];
		
		for (std::vector<int>::iterator it = vTried.begin(); it != vTried.end(); it++)
		{
			if (!setTried.count(*it))
			{
				return -11;
			}
			
			setTried.erase(*it);
		}
	}

	for (int n = 0; n < vvNew.size(); n++)
	{
		std::set<int> &vNew = vvNew[n];
		
		for (std::set<int>::iterator it = vNew.begin(); it != vNew.end(); it++)
		{
			if (!mapNew.count(*it))
			{
				return -12;
			}
			
			if (--mapNew[*it] == 0)
			{
				mapNew.erase(*it);
			}
		}
	}

	if (setTried.size())
	{
		return -13;
	}

	if (mapNew.size())
	{
		return -15;
	}

	return 0;
}
#endif // DEBUG_ADDRMAN

void CAddrMan::GetAddr_(std::vector<CAddress> &vAddr)
{
	int nNodes = ADDRMAN_GETADDR_MAX_PCT * vRandom.size() / 100;

	if (nNodes > ADDRMAN_GETADDR_MAX)
	{
		nNodes = ADDRMAN_GETADDR_MAX;
	}

	// perform a random shuffle over the first nNodes elements of vRandom (selecting from all)
	for (int n = 0; n < nNodes; n++)
	{
		int nRndPos = GetRandInt(vRandom.size() - n) + n;
		
		SwapRandom(n, nRndPos);
		
		assert(mapInfo.count(vRandom[n]) == 1);
		
		vAddr.push_back(mapInfo[vRandom[n]]);
	}
}

void CAddrMan::Connected_(const CService &addr, int64_t nTime)
{
	CAddrInfo *pinfo = Find(addr);

	// if not found, bail out
	if (!pinfo)
	{
		return;
	}

	CAddrInfo &info = *pinfo;

	// check whether we are talking about the exact same CService (including same port)
	if (info != addr)
	{
		return;
	}

	// update info
	int64_t nUpdateInterval = 20 * 60;
	if (nTime - info.nTime > nUpdateInterval)
	{
		info.nTime = nTime;
	}
}

CAddrMan::CAddrMan() :
		vRandom(0),
		vvTried(ADDRMAN_TRIED_BUCKET_COUNT, std::vector<int>(0)),
		vvNew(ADDRMAN_NEW_BUCKET_COUNT, std::set<int>())
{
	nKey.resize(32);
	GetRandBytes(&nKey[0], 32);

	nIdCount = 0;
	nTried = 0;
	nNew = 0;
}

// Return the number of (unique) addresses in all tables.
int CAddrMan::size()
{
	return vRandom.size();
}

// Consistency check
void CAddrMan::Check()
{
#ifdef DEBUG_ADDRMAN
	{
		LOCK(cs);
		
		int err;
		
		if ((err = Check_()))
		{
			LogPrintf("ADDRMAN CONSISTENCY CHECK FAILED!!! err=%i\n", err);
		}
	}
#endif
}

// Add a single address.
bool CAddrMan::Add(const CAddress &addr, const CNetAddr& source, int64_t nTimePenalty)
{
	bool fRet = false;

	{
		LOCK(cs);
		
		Check();
		
		fRet |= Add_(addr, source, nTimePenalty);
		
		Check();
	}

	if (fRet)
	{
		LogPrint("addrman", "Added %s from %s: %i tried, %i new\n", addr.ToStringIPPort(), source.ToString(), nTried, nNew);
	}

	return fRet;
}

// Add multiple addresses.
bool CAddrMan::Add(const std::vector<CAddress> &vAddr, const CNetAddr& source, int64_t nTimePenalty)
{
	int nAdd = 0;

	{
		LOCK(cs);
		
		Check();
		
		for (std::vector<CAddress>::const_iterator it = vAddr.begin(); it != vAddr.end(); it++)
		{
			nAdd += Add_(*it, source, nTimePenalty) ? 1 : 0;
		}
		
		Check();
	}

	if (nAdd)
	{
		LogPrint("addrman", "Added %i addresses from %s: %i tried, %i new\n", nAdd, source.ToString(), nTried, nNew);
	}

	return nAdd > 0;
}

// Mark an entry as accessible.
void CAddrMan::Good(const CService &addr, int64_t nTime)
{
	{
		LOCK(cs);

		Check();

		Good_(addr, nTime);

		Check();
	}
}

// Mark an entry as connection attempted to.
void CAddrMan::Attempt(const CService &addr, int64_t nTime)
{
	{
		LOCK(cs);
		
		Check();
		
		Attempt_(addr, nTime);
		
		Check();
	}
}

// Choose an address to connect to.
// nUnkBias determines how much "new" entries are favored over "tried" ones (0-100).
CAddress CAddrMan::Select(int nUnkBias)
{
	CAddress addrRet;
	{
		LOCK(cs);
		
		Check();
		
		addrRet = Select_(nUnkBias);
		
		Check();
	}

	return addrRet;
}

// Return a bunch of addresses, selected at random.
std::vector<CAddress> CAddrMan::GetAddr()
{
	Check();
	
	std::vector<CAddress> vAddr;
	
	{
		LOCK(cs);
		
		GetAddr_(vAddr);
	}
	
	Check();
	
	return vAddr;
}

// Mark an entry as currently-connected-to.
void CAddrMan::Connected(const CService &addr, int64_t nTime)
{
	{
		LOCK(cs);
		
		Check();
		
		Connected_(addr, nTime);
		
		Check();
	}
}

unsigned int CAddrMan::GetSerializeSize(int nType, int nVersion) const
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
	
	// serialized format:
	// * version byte (currently 0)
	// * nKey
	// * nNew
	// * nTried
	// * number of "new" buckets
	// * all nNew addrinfos in vvNew
	// * all nTried addrinfos in vvTried
	// * for each bucket:
	//   * number of elements
	//   * for each element: index
	//
	// Notice that vvTried, mapAddr and vVector are never encoded explicitly;
	// they are instead reconstructed from the other information.
	//
	// vvNew is serialized, but only used if ADDRMAN_UNKOWN_BUCKET_COUNT didn't change,
	// otherwise it is reconstructed as well.
	//
	// This format is more complex, but significantly smaller (at most 1.5 MiB), and supports
	// changes to the ADDRMAN_ parameters without breaking the on-disk structure.
	{
		LOCK(cs);
		
		unsigned char nVersion = 0;
		
		READWRITE(nVersion);
		READWRITE(nKey);
		READWRITE(nNew);
		READWRITE(nTried);

		CAddrMan *am = const_cast<CAddrMan*>(this);
		
		if (fWrite)
		{
			int nUBuckets = ADDRMAN_NEW_BUCKET_COUNT;
			std::map<int, int> mapUnkIds;
			int nIds = 0;
			
			READWRITE(nUBuckets);
			
			for (std::map<int, CAddrInfo>::iterator it = am->mapInfo.begin(); it != am->mapInfo.end(); it++)
			{
				if (nIds == nNew)
				{
					break; // this means nNew was wrong, oh ow
				}
				
				mapUnkIds[(*it).first] = nIds;
				CAddrInfo &info = (*it).second;
				
				if (info.nRefCount)
				{
					READWRITE(info);
					nIds++;
				}
			}
			
			nIds = 0;
			
			for (std::map<int, CAddrInfo>::iterator it = am->mapInfo.begin(); it != am->mapInfo.end(); it++)
			{
				if (nIds == nTried)
				{
					break; // this means nTried was wrong, oh ow
				}
				
				CAddrInfo &info = (*it).second;
				
				if (info.fInTried)
				{
					READWRITE(info);
					nIds++;
				}
			}
			
			for (std::vector<std::set<int> >::iterator it = am->vvNew.begin(); it != am->vvNew.end(); it++)
			{
				const std::set<int> &vNew = (*it);
				int nSize = vNew.size();
				
				READWRITE(nSize);
				
				for (std::set<int>::iterator it2 = vNew.begin(); it2 != vNew.end(); it2++)
				{
					int nIndex = mapUnkIds[*it2];
					READWRITE(nIndex);
				}
			}
		}
		else
		{
			int nUBuckets = 0;
			
			READWRITE(nUBuckets);
			
			am->nIdCount = 0;
			am->mapInfo.clear();
			am->mapAddr.clear();
			am->vRandom.clear();
			am->vvTried = std::vector<std::vector<int> >(ADDRMAN_TRIED_BUCKET_COUNT, std::vector<int>(0));
			am->vvNew = std::vector<std::set<int> >(ADDRMAN_NEW_BUCKET_COUNT, std::set<int>());
			
			for (int n = 0; n < am->nNew; n++)
			{
				CAddrInfo &info = am->mapInfo[n];
				
				READWRITE(info);
				
				am->mapAddr[info] = n;
				info.nRandomPos = vRandom.size();
				am->vRandom.push_back(n);
				
				if (nUBuckets != ADDRMAN_NEW_BUCKET_COUNT)
				{
					am->vvNew[info.GetNewBucket(am->nKey)].insert(n);
					info.nRefCount++;
				}
			}
			
			am->nIdCount = am->nNew;
			
			int nLost = 0;
			for (int n = 0; n < am->nTried; n++)
			{
				CAddrInfo info;
				
				READWRITE(info);
				
				std::vector<int> &vTried = am->vvTried[info.GetTriedBucket(am->nKey)];
				
				if (vTried.size() < ADDRMAN_TRIED_BUCKET_SIZE)
				{
					info.nRandomPos = vRandom.size();
					info.fInTried = true;
					
					am->vRandom.push_back(am->nIdCount);
					am->mapInfo[am->nIdCount] = info;
					am->mapAddr[info] = am->nIdCount;
					
					vTried.push_back(am->nIdCount);
					
					am->nIdCount++;
				}
				else
				{
					nLost++;
				}
			}
			
			am->nTried -= nLost;
			
			for (int b = 0; b < nUBuckets; b++)
			{
				std::set<int> &vNew = am->vvNew[b];
				int nSize = 0;
				
				READWRITE(nSize);
				
				for (int n = 0; n < nSize; n++)
				{
					int nIndex = 0;
					
					READWRITE(nIndex);
					
					CAddrInfo &info = am->mapInfo[nIndex];
					
					if (nUBuckets == ADDRMAN_NEW_BUCKET_COUNT && info.nRefCount < ADDRMAN_NEW_BUCKETS_PER_ADDRESS)
					{
						info.nRefCount++;
						vNew.insert(nIndex);
					}
				}
			}
		}
	}
	
	return nSerSize;
}

template<typename Stream>
void CAddrMan::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	// serialized format:
	// * version byte (currently 0)
	// * nKey
	// * nNew
	// * nTried
	// * number of "new" buckets
	// * all nNew addrinfos in vvNew
	// * all nTried addrinfos in vvTried
	// * for each bucket:
	//   * number of elements
	//   * for each element: index
	//
	// Notice that vvTried, mapAddr and vVector are never encoded explicitly;
	// they are instead reconstructed from the other information.
	//
	// vvNew is serialized, but only used if ADDRMAN_UNKOWN_BUCKET_COUNT didn't change,
	// otherwise it is reconstructed as well.
	//
	// This format is more complex, but significantly smaller (at most 1.5 MiB), and supports
	// changes to the ADDRMAN_ parameters without breaking the on-disk structure.
	{
		LOCK(cs);
		
		unsigned char nVersion = 0;
		
		READWRITE(nVersion);
		READWRITE(nKey);
		READWRITE(nNew);
		READWRITE(nTried);

		CAddrMan *am = const_cast<CAddrMan*>(this);
		if (fWrite)
		{
			int nUBuckets = ADDRMAN_NEW_BUCKET_COUNT;
			std::map<int, int> mapUnkIds;
			int nIds = 0;
			
			READWRITE(nUBuckets);
			
			for (std::map<int, CAddrInfo>::iterator it = am->mapInfo.begin(); it != am->mapInfo.end(); it++)
			{
				if (nIds == nNew)
				{
					break; // this means nNew was wrong, oh ow
				}
				
				mapUnkIds[(*it).first] = nIds;
				CAddrInfo &info = (*it).second;
				
				if (info.nRefCount)
				{
					READWRITE(info);
					nIds++;
				}
			}
			
			nIds = 0;
			
			for (std::map<int, CAddrInfo>::iterator it = am->mapInfo.begin(); it != am->mapInfo.end(); it++)
			{
				if (nIds == nTried)
				{
					break; // this means nTried was wrong, oh ow
				}
				
				CAddrInfo &info = (*it).second;
				
				if (info.fInTried)
				{
					READWRITE(info);
					nIds++;
				}
			}
			
			for (std::vector<std::set<int> >::iterator it = am->vvNew.begin(); it != am->vvNew.end(); it++)
			{
				const std::set<int> &vNew = (*it);
				int nSize = vNew.size();
				
				READWRITE(nSize);
				
				for (std::set<int>::iterator it2 = vNew.begin(); it2 != vNew.end(); it2++)
				{
					int nIndex = mapUnkIds[*it2];
					READWRITE(nIndex);
				}
			}
		}
		else
		{
			int nUBuckets = 0;
			
			READWRITE(nUBuckets);
			
			am->nIdCount = 0;
			am->mapInfo.clear();
			am->mapAddr.clear();
			am->vRandom.clear();
			am->vvTried = std::vector<std::vector<int> >(ADDRMAN_TRIED_BUCKET_COUNT, std::vector<int>(0));
			am->vvNew = std::vector<std::set<int> >(ADDRMAN_NEW_BUCKET_COUNT, std::set<int>());
			
			for (int n = 0; n < am->nNew; n++)
			{
				CAddrInfo &info = am->mapInfo[n];
				
				READWRITE(info);
				
				am->mapAddr[info] = n;
				info.nRandomPos = vRandom.size();
				am->vRandom.push_back(n);
				
				if (nUBuckets != ADDRMAN_NEW_BUCKET_COUNT)
				{
					am->vvNew[info.GetNewBucket(am->nKey)].insert(n);
					info.nRefCount++;
				}
			}
			
			am->nIdCount = am->nNew;
			
			int nLost = 0;
			for (int n = 0; n < am->nTried; n++)
			{
				CAddrInfo info;
				
				READWRITE(info);
				
				std::vector<int> &vTried = am->vvTried[info.GetTriedBucket(am->nKey)];
				
				if (vTried.size() < ADDRMAN_TRIED_BUCKET_SIZE)
				{
					info.nRandomPos = vRandom.size();
					info.fInTried = true;
					
					am->vRandom.push_back(am->nIdCount);
					am->mapInfo[am->nIdCount] = info;
					am->mapAddr[info] = am->nIdCount;
					
					vTried.push_back(am->nIdCount);
					
					am->nIdCount++;
				}
				else
				{
					nLost++;
				}
			}
			
			am->nTried -= nLost;
			
			for (int b = 0; b < nUBuckets; b++)
			{
				std::set<int> &vNew = am->vvNew[b];
				int nSize = 0;
				
				READWRITE(nSize);
				
				for (int n = 0; n < nSize; n++)
				{
					int nIndex = 0;
					
					READWRITE(nIndex);
					
					CAddrInfo &info = am->mapInfo[nIndex];
					
					if (nUBuckets == ADDRMAN_NEW_BUCKET_COUNT && info.nRefCount < ADDRMAN_NEW_BUCKETS_PER_ADDRESS)
					{
						info.nRefCount++;
						vNew.insert(nIndex);
					}
				}
			}
		}
	}
}

template<typename Stream>
void CAddrMan::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	// serialized format:
	// * version byte (currently 0)
	// * nKey
	// * nNew
	// * nTried
	// * number of "new" buckets
	// * all nNew addrinfos in vvNew
	// * all nTried addrinfos in vvTried
	// * for each bucket:
	//   * number of elements
	//   * for each element: index
	//
	// Notice that vvTried, mapAddr and vVector are never encoded explicitly;
	// they are instead reconstructed from the other information.
	//
	// vvNew is serialized, but only used if ADDRMAN_UNKOWN_BUCKET_COUNT didn't change,
	// otherwise it is reconstructed as well.
	//
	// This format is more complex, but significantly smaller (at most 1.5 MiB), and supports
	// changes to the ADDRMAN_ parameters without breaking the on-disk structure.
	{
		LOCK(cs);
		
		unsigned char nVersion = 0;
		
		READWRITE(nVersion);
		READWRITE(nKey);
		READWRITE(nNew);
		READWRITE(nTried);

		CAddrMan *am = const_cast<CAddrMan*>(this);
		if (fWrite)
		{
			int nUBuckets = ADDRMAN_NEW_BUCKET_COUNT;
			std::map<int, int> mapUnkIds;
			int nIds = 0;
			
			READWRITE(nUBuckets);
			
			for (std::map<int, CAddrInfo>::iterator it = am->mapInfo.begin(); it != am->mapInfo.end(); it++)
			{
				if (nIds == nNew)
				{
					break; // this means nNew was wrong, oh ow
				}
				
				mapUnkIds[(*it).first] = nIds;
				CAddrInfo &info = (*it).second;
				
				if (info.nRefCount)
				{
					READWRITE(info);
					nIds++;
				}
			}
			
			nIds = 0;
			
			for (std::map<int, CAddrInfo>::iterator it = am->mapInfo.begin(); it != am->mapInfo.end(); it++)
			{
				if (nIds == nTried)
				{
					break; // this means nTried was wrong, oh ow
				}
				
				CAddrInfo &info = (*it).second;
				
				if (info.fInTried)
				{
					READWRITE(info);
					nIds++;
				}
			}
			
			for (std::vector<std::set<int> >::iterator it = am->vvNew.begin(); it != am->vvNew.end(); it++)
			{
				const std::set<int> &vNew = (*it);
				int nSize = vNew.size();
				
				READWRITE(nSize);
				
				for (std::set<int>::iterator it2 = vNew.begin(); it2 != vNew.end(); it2++)
				{
					int nIndex = mapUnkIds[*it2];
					READWRITE(nIndex);
				}
			}
		}
		else
		{
			int nUBuckets = 0;
			
			READWRITE(nUBuckets);
			
			am->nIdCount = 0;
			am->mapInfo.clear();
			am->mapAddr.clear();
			am->vRandom.clear();
			am->vvTried = std::vector<std::vector<int> >(ADDRMAN_TRIED_BUCKET_COUNT, std::vector<int>(0));
			am->vvNew = std::vector<std::set<int> >(ADDRMAN_NEW_BUCKET_COUNT, std::set<int>());
			
			for (int n = 0; n < am->nNew; n++)
			{
				CAddrInfo &info = am->mapInfo[n];
				
				READWRITE(info);
				
				am->mapAddr[info] = n;
				info.nRandomPos = vRandom.size();
				am->vRandom.push_back(n);
				
				if (nUBuckets != ADDRMAN_NEW_BUCKET_COUNT)
				{
					am->vvNew[info.GetNewBucket(am->nKey)].insert(n);
					info.nRefCount++;
				}
			}
			
			am->nIdCount = am->nNew;
			
			int nLost = 0;
			for (int n = 0; n < am->nTried; n++)
			{
				CAddrInfo info;
				
				READWRITE(info);
				
				std::vector<int> &vTried = am->vvTried[info.GetTriedBucket(am->nKey)];
				
				if (vTried.size() < ADDRMAN_TRIED_BUCKET_SIZE)
				{
					info.nRandomPos = vRandom.size();
					info.fInTried = true;
					
					am->vRandom.push_back(am->nIdCount);
					am->mapInfo[am->nIdCount] = info;
					am->mapAddr[info] = am->nIdCount;
					
					vTried.push_back(am->nIdCount);
					
					am->nIdCount++;
				}
				else
				{
					nLost++;
				}
			}
			
			am->nTried -= nLost;
			
			for (int b = 0; b < nUBuckets; b++)
			{
				std::set<int> &vNew = am->vvNew[b];
				int nSize = 0;
				
				READWRITE(nSize);
				
				for (int n = 0; n < nSize; n++)
				{
					int nIndex = 0;
					
					READWRITE(nIndex);
					
					CAddrInfo &info = am->mapInfo[nIndex];
					
					if (nUBuckets == ADDRMAN_NEW_BUCKET_COUNT && info.nRefCount < ADDRMAN_NEW_BUCKETS_PER_ADDRESS)
					{
						info.nRefCount++;
						vNew.insert(nIndex);
					}
				}
			}
		}
	}
}

template void CAddrMan::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CAddrMan::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

