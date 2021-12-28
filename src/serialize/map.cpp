#include "base.h"
#include "read.h"
#include "write.h"
#include "cdatastream.h"
#include "coutpoint.h"
#include "net/cnetaddr.h"
#include "net/csubnet.h"
#include "net/cbanentry.h"
#include "pair.h"

#include "map.h"

template<typename K, typename T, typename Pred, typename A>
unsigned int GetSerializeSize(const std::map<K, T, Pred, A>& m, int nType, int nVersion)
{
	unsigned int nSize = GetSizeOfCompactSize(m.size());

	for (typename std::map<K, T, Pred, A>::const_iterator mi = m.begin(); mi != m.end(); ++mi)
	{
		nSize += GetSerializeSize((*mi), nType, nVersion);
	}
	
	return nSize;
}

template<typename Stream, typename K, typename T, typename Pred, typename A>
void Serialize(Stream& os, const std::map<K, T, Pred, A>& m, int nType, int nVersion)
{
	WriteCompactSize(os, m.size());

	for (typename std::map<K, T, Pred, A>::const_iterator mi = m.begin(); mi != m.end(); ++mi)
	{
		Serialize(os, (*mi), nType, nVersion);
	}
}

template<typename Stream, typename K, typename T, typename Pred, typename A>
void Unserialize(Stream& is, std::map<K, T, Pred, A>& m, int nType, int nVersion)
{
	m.clear();

	unsigned int nSize = ReadCompactSize(is);
	typename std::map<K, T, Pred, A>::iterator mi = m.begin();

	for (unsigned int i = 0; i < nSize; i++)
	{
		std::pair<K, T> item;
		
		Unserialize(is, item, nType, nVersion);
		
		mi = m.insert(mi, item);
	}
}



//
// Generate Templates
//

// GetSerializeSize
#define TmpGetSerializeSize(A, B)	template unsigned int GetSerializeSize<A, B, std::less<A>, std::allocator<std::pair<const A, B>>>( \
		const std::map<A, B, std::less<A>, std::allocator<std::pair<const A, B>>>&, int, int);

TmpGetSerializeSize(std::string, std::string);
TmpGetSerializeSize(CNetAddr, long);
TmpGetSerializeSize(COutPoint, long);

// Serialize
#define TmpSerialize(A, B)	template void Serialize<CDataStream, A, B, std::less<A>, std::allocator<std::pair<const A, B>>>( \
		CDataStream&, const std::map<A, B, std::less<A>, std::allocator<std::pair<const A, B>>>&, int, int);

TmpSerialize(std::string, std::string)
TmpSerialize(CNetAddr, long)
TmpSerialize(COutPoint, long)
TmpSerialize(CSubNet, CBanEntry)

// Unserialize
#define TmpUnserialize(A, B)	template void Unserialize<CDataStream, A, B, std::less<A>, std::allocator<std::pair<const A, B>>>( \
		CDataStream&, std::map<A, B, std::less<A>, std::allocator<std::pair<const A, B>>>&, int, int);
		
TmpUnserialize(std::string, std::string)
TmpUnserialize(CNetAddr, long)
TmpUnserialize(COutPoint, long)
TmpUnserialize(CSubNet, CBanEntry)

