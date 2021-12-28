#include <string>

#include "read.h"
#include "write.h"
#include "string.h"
#include "vector.h"
#include "cdatastream.h"
#include "coutpoint.h"
#include "net/cnetaddr.h"
#include "net/csubnet.h"
#include "net/cbanentry.h"
#include "uint/uint160.h"
#include "uint/uint256.h"
#include "allocators/secure_allocator.h"
#include "cscript.h"
#include "cpubkey.h"
#include "ckeyid.h"

#include "pair.h"

template<typename K, typename T>
unsigned int GetSerializeSize(const std::pair<K, T>& item, int nType, int nVersion)
{
	return GetSerializeSize(item.first, nType, nVersion) + GetSerializeSize(item.second, nType, nVersion);
}

template<typename Stream, typename K, typename T>
void Serialize(Stream& os, const std::pair<K, T>& item, int nType, int nVersion)
{
	Serialize(os, item.first, nType, nVersion);
	Serialize(os, item.second, nType, nVersion);
}

template<typename Stream, typename K, typename T>
void Unserialize(Stream& is, std::pair<K, T>& item, int nType, int nVersion)
{
	Unserialize(is, item.first, nType, nVersion);
	Unserialize(is, item.second, nType, nVersion);
}



//
// Generate Templates
//

// GetSerializeSize
#define TmpGetSerializeSize(A, B)	template unsigned int GetSerializeSize<A, B>(const std::pair<A, B>&, int, int);

TmpGetSerializeSize(const std::string, std::string);
TmpGetSerializeSize(std::string,		std::string);
TmpGetSerializeSize(const CNetAddr,		int64_t);
TmpGetSerializeSize(const COutPoint,	int64_t);

// Serialize
#define TmpSerialize(A, B, C)	template void Serialize<A, B, C>(A&, const std::pair<B, C>&, int, int);

TmpSerialize(CDataStream, std::string, unsigned int);
TmpSerialize(CDataStream, std::string, long);
TmpSerialize(CDataStream, std::string, long long);
TmpSerialize(CDataStream, std::string, std::string);
TmpSerialize(CDataStream, std::string, CKeyID);
TmpSerialize(CDataStream, std::string, CPubKey);
TmpSerialize(CDataStream, std::string, CScript);
TmpSerialize(CDataStream, std::string, uint160);
TmpSerialize(CDataStream, std::string, uint256);
TmpSerialize(CDataStream, const CNetAddr, int64_t);
TmpSerialize(CDataStream, const COutPoint, int64_t);
TmpSerialize(CDataStream, const CSubNet, CBanEntry);
TmpSerialize(CDataStream, const std::string, std::string);
template void Serialize<CDataStream, std::string, std::vector<unsigned char, std::allocator<unsigned char>>>(
		CDataStream&, std::pair<std::string, std::vector<unsigned char, std::allocator<unsigned char>>> const&, int, int);
template void Serialize<CDataStream, std::vector<unsigned char, secure_allocator<unsigned char>>, uint256>(
		CDataStream&, std::pair<std::vector<unsigned char, secure_allocator<unsigned char>>, uint256> const&, int, int);


// Unserialize
#define TmpUnserialize(A, B, C)	template void Unserialize<A, B, C>(A&, std::pair<B, C>&, int, int);

TmpUnserialize(CDataStream, std::string, std::string);
TmpUnserialize(CDataStream, CNetAddr, int64_t);
TmpUnserialize(CDataStream, COutPoint, int64_t);
TmpUnserialize(CDataStream, CSubNet, CBanEntry);

