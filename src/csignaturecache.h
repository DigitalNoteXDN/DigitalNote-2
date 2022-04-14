#ifndef CSIGNATURECACHE_H
#define CSIGNATURECACHE_H

#include <set>
#include <vector>
#include <boost/thread/shared_mutex.hpp>
#include <boost/tuple/tuple.hpp>

class uint256;
class CPubKey;

// Valid signature cache, to avoid doing expensive ECDSA signature checking
// twice for every transaction (once when accepted into memory pool, and
// again when accepted into the block chain)

class CSignatureCache
{
private:
	 // sigdata_type is (signature hash, signature, public key):
	typedef boost::tuple<uint256, std::vector<unsigned char>, CPubKey> sigdata_type;

	std::set<sigdata_type> setValid;
	boost::shared_mutex cs_sigcache;

public:
	bool Get(const uint256 &hash, const std::vector<unsigned char>& vchSig, const CPubKey& pubKey);
	void Set(const uint256 &hash, const std::vector<unsigned char>& vchSig, const CPubKey& pubKey);
};

#endif // CSIGNATURECACHE_H
