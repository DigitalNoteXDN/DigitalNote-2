#include "compat.h"

#include <boost/tuple/tuple_comparison.hpp>

#include "uint/uint256.h"
#include "util.h"
#include "cpubkey.h"

#include "csignaturecache.h"

bool CSignatureCache::Get(const uint256 &hash, const std::vector<unsigned char>& vchSig, const CPubKey& pubKey)
{
	boost::shared_lock<boost::shared_mutex> lock(cs_sigcache);

	sigdata_type k(hash, vchSig, pubKey);
	std::set<sigdata_type>::iterator mi = setValid.find(k);
	
	if (mi != setValid.end())
	{
		return true;
	}
	
	return false;
}

void CSignatureCache::Set(const uint256 &hash, const std::vector<unsigned char>& vchSig, const CPubKey& pubKey)
{
	// DoS prevention: limit cache size to less than 10MB
	// (~200 bytes per cache entry times 50,000 entries)
	// Since there are a maximum of 20,000 signature operations per block
	// 50,000 is a reasonable default.
	int64_t nMaxCacheSize = GetArg("-maxsigcachesize", 50000);
	
	if (nMaxCacheSize <= 0)
	{
		return;
	}
	
	boost::unique_lock<boost::shared_mutex> lock(cs_sigcache);

	while (static_cast<int64_t>(setValid.size()) > nMaxCacheSize)
	{
		// Evict a random entry. Random because that helps
		// foil would-be DoS attackers who might try to pre-generate
		// and re-use a set of valid signatures just-slightly-greater
		// than our cache size.
		uint256 randomHash = GetRandHash();
		std::vector<unsigned char> unused;
		std::set<sigdata_type>::iterator it = setValid.lower_bound(sigdata_type(randomHash, unused, unused));
		
		if (it == setValid.end())
		{
			it = setValid.begin();
		}
		
		setValid.erase(*it);
	}

	sigdata_type k(hash, vchSig, pubKey);
	
	setValid.insert(k);
}

