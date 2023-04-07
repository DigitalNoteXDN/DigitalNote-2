#include "base.h"
#include "read.h"
#include "write.h"
#include "string.h"
#include "cdatastream.h"

#include "set.h"

template<typename K, typename Pred, typename A>
unsigned int GetSerializeSize(const std::set<K, Pred, A>& m, int nType, int nVersion)
{
	unsigned int nSize = GetSizeOfCompactSize(m.size());

	for (typename std::set<K, Pred, A>::const_iterator it = m.begin(); it != m.end(); ++it)
	{
		nSize += GetSerializeSize((*it), nType, nVersion);
	}

	return nSize;
}

template<typename Stream, typename K, typename Pred, typename A>
void Serialize(Stream& os, const std::set<K, Pred, A>& m, int nType, int nVersion)
{
	WriteCompactSize(os, m.size());

	for (typename std::set<K, Pred, A>::const_iterator it = m.begin(); it != m.end(); ++it)
	{
		Serialize(os, (*it), nType, nVersion);
	}
}

template<typename Stream, typename K, typename Pred, typename A>
void Unserialize(Stream& is, std::set<K, Pred, A>& m, int nType, int nVersion)
{
	m.clear();

	unsigned int nSize = ReadCompactSize(is);
	typename std::set<K, Pred, A>::iterator it = m.begin();

	for (unsigned int i = 0; i < nSize; i++)
	{
		K key;
		
		Unserialize(is, key, nType, nVersion);
		
		it = m.insert(it, key);
	}
}

// GetSerializeSize
#define TmpGetSerializeSize(A)	template unsigned int GetSerializeSize<A, std::less<A>, std::allocator<A>>( \
		std::set<A, std::less<A>, std::allocator<A>> const&, int, int);

TmpGetSerializeSize(int);
TmpGetSerializeSize(std::string);

// Serialize
#define TmpSerialize(A)		template void Serialize<CDataStream, A, std::less<A>, std::allocator<A>>( \
		CDataStream&, std::set<A, std::less<A>, std::allocator<A>> const&, int, int);

TmpSerialize(int);
TmpSerialize(std::string);

// Unserialize
#define TmpUnserialize(A)		template void Unserialize<CDataStream, A, std::less<A>, std::allocator<A>>( \
		CDataStream&, std::set<A, std::less<A>, std::allocator<A>>&, int, int);

TmpUnserialize(int);
TmpUnserialize(std::string);

