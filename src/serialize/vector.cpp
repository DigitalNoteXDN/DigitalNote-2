#include <boost/type_traits/is_fundamental.hpp>

#include "base.h"
#include "read.h"
#include "write.h"
#include "cdatastream.h"
#include "csizecomputer.h"
#include "cmasternode.h"
#include "allocators/secure_allocator.h"
#include "caddress.h"
#include "cinv.h"
#include "uint/uint256.h"
#include "ctxout.h"
#include "cblock.h"
#include "chashwriter.h"
#include "cautofile.h"
#include "cdisktxpos.h"
#include "ctransaction.h"
#include "pair.h"
#include "cmerkletx.h"
#include "vector.h"

template<typename T, typename A>
unsigned int GetSerializeSize_impl(const std::vector<T, A>& v, int nType, int nVersion, const boost::true_type&)
{
	return (GetSizeOfCompactSize(v.size()) + v.size() * sizeof(T));
}

template<typename T, typename A>
unsigned int GetSerializeSize_impl(const std::vector<T, A>& v, int nType, int nVersion, const boost::false_type&)
{
	unsigned int nSize = GetSizeOfCompactSize(v.size());

	for (typename std::vector<T, A>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
	{
		nSize += GetSerializeSize((*vi), nType, nVersion);
	}

	return nSize;
}

template<typename T, typename A>
unsigned int GetSerializeSize(const std::vector<T, A>& v, int nType, int nVersion)
{
	return GetSerializeSize_impl(v, nType, nVersion, boost::is_fundamental<T>());
}

template<typename Stream, typename T, typename A>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, int nType, int nVersion, const boost::true_type&)
{
	WriteCompactSize(os, v.size());
	
	if (!v.empty())
	{
		os.write((char*)&v[0], v.size() * sizeof(T));
	}
}

template<typename Stream, typename T, typename A>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, int nType, int nVersion, const boost::false_type&)
{
	WriteCompactSize(os, v.size());

	for (typename std::vector<T, A>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
	{
		::Serialize(os, (*vi), nType, nVersion);
	}
}

template<typename Stream, typename T, typename A>
void Serialize(Stream& os, const std::vector<T, A>& v, int nType, int nVersion)
{
	Serialize_impl(os, v, nType, nVersion, boost::is_fundamental<T>());
}

template<typename Stream, typename T, typename A>
void Unserialize_impl(Stream& is, std::vector<T, A>& v, int nType, int nVersion, const boost::true_type&)
{
	// Limit size per read so bogus size value won't cause out of memory
	v.clear();

	unsigned int nSize = ReadCompactSize(is);
	unsigned int i = 0;

	while (i < nSize)
	{
		unsigned int blk = std::min(nSize - i, (unsigned int)(1 + 4999999 / sizeof(T)));
		
		v.resize(i + blk);
		
		is.read((char*)&v[i], blk * sizeof(T));
		
		i += blk;
	}
}

template<typename Stream, typename T, typename A>
void Unserialize_impl(Stream& is, std::vector<T, A>& v, int nType, int nVersion, const boost::false_type&)
{
    v.clear();
	
    unsigned int nSize = ReadCompactSize(is);
    unsigned int i = 0;
    unsigned int nMid = 0;
	
    while (nMid < nSize)
    {
        nMid += 5000000 / sizeof(T);
		
        if (nMid > nSize)
		{
            nMid = nSize;
		}
		
        v.resize(nMid);
        
		for (; i < nMid; i++)
		{
            Unserialize(is, v[i], nType, nVersion);
		}
    }
}

template<typename Stream, typename T, typename A>
void Unserialize(Stream& is, std::vector<T, A>& v, int nType, int nVersion)
{
	Unserialize_impl(is, v, nType, nVersion, boost::is_fundamental<T>());
}



//
// others derived from vector
//
unsigned int GetSerializeSize(const CScript& v, int nType, int nVersion)
{
	return GetSerializeSize((const std::vector<unsigned char>&)v, nType, nVersion);
}

template<typename Stream>
void Serialize(Stream& os, const CScript& v, int nType, int nVersion)
{
	Serialize(os, (const std::vector<unsigned char>&)v, nType, nVersion);
}

template<typename Stream>
void Unserialize(Stream& is, CScript& v, int nType, int nVersion)
{
	Unserialize(is, (std::vector<unsigned char>&)v, nType, nVersion);
}



//
// Generate Templates
//

// GetSerializeSize
#define TmpGetSerializeSize(A, B)	template unsigned int GetSerializeSize<A, B>(std::vector<A, B> const&, int, int);

TmpGetSerializeSize(unsigned char,	std::allocator<unsigned char>);
TmpGetSerializeSize(CDiskTxPos,		std::allocator<CDiskTxPos>);
TmpGetSerializeSize(CMasternode,	std::allocator<CMasternode>);
TmpGetSerializeSize(CMerkleTx,		std::allocator<CMerkleTx>);
TmpGetSerializeSize(CTransaction,	std::allocator<CTransaction>);
TmpGetSerializeSize(CTxIn,			std::allocator<CTxIn>);
TmpGetSerializeSize(CTxOut,			std::allocator<CTxOut>);
TmpGetSerializeSize(uint256,		std::allocator<uint256>);
TmpGetSerializeSize(unsigned char,	secure_allocator<unsigned char>);

template unsigned int GetSerializeSize<std::pair<std::string, std::string>, std::allocator<std::pair<std::string, std::string>>>(
		std::vector<std::pair<std::string, std::string>, std::allocator<std::pair<std::string, std::string>>> const&, int, int);

// Serialize
#define TmpSerialize(A, B, C)		template void Serialize<A, B, C>(A&, const std::vector<B, C>&, int, int);

TmpSerialize(CDataStream,	unsigned char,	std::allocator<unsigned char>);
TmpSerialize(CDataStream,	unsigned char,	secure_allocator<unsigned char>);
TmpSerialize(CDataStream,	CAddress, 		std::allocator<CAddress>);
TmpSerialize(CDataStream,	CBlock,			std::allocator<CBlock>);
TmpSerialize(CDataStream,	CDiskTxPos,		std::allocator<CDiskTxPos>);
TmpSerialize(CDataStream,	CInv,			std::allocator<CInv>);
TmpSerialize(CDataStream,	CMasternode,	std::allocator<CMasternode>);
TmpSerialize(CDataStream,	CMerkleTx,		std::allocator<CMerkleTx>);
TmpSerialize(CDataStream,	CTransaction,	std::allocator<CTransaction>);
TmpSerialize(CDataStream,	CTxIn,			std::allocator<CTxIn>);
TmpSerialize(CDataStream,	CTxOut,			std::allocator<CTxOut>);
TmpSerialize(CDataStream,	uint256,		std::allocator<uint256>);

template void Serialize<CDataStream, std::pair<std::string, std::string>, std::allocator<std::pair<std::string, std::string>>>(
		CDataStream&, std::vector<std::pair<std::string, std::string>, std::allocator<std::pair<std::string, std::string>>> const&, int, int);

TmpSerialize(CAutoFile, 	unsigned char,	std::allocator<unsigned char>);
TmpSerialize(CAutoFile, 	CDiskTxPos,		std::allocator<CDiskTxPos>);
TmpSerialize(CAutoFile, 	CTransaction,	std::allocator<CTransaction>);
TmpSerialize(CAutoFile, 	CTxIn,			std::allocator<CTxIn>);
TmpSerialize(CAutoFile, 	CTxOut,			std::allocator<CTxOut>);

TmpSerialize(CHashWriter,	unsigned char,	std::allocator<unsigned char>);
TmpSerialize(CHashWriter,	CDiskTxPos,		std::allocator<CDiskTxPos>);
TmpSerialize(CHashWriter,	CTxIn,			std::allocator<CTxIn>);
TmpSerialize(CHashWriter,	CTxOut,			std::allocator<CTxOut>);

TmpSerialize(CSizeComputer, unsigned char,	std::allocator<unsigned char>);

// Unserialize
#define TmpUnserialize(A, B, C)		template void Unserialize<A, B, C>(A&, std::vector<B, C>&, int, int);

TmpUnserialize(CDataStream,	unsigned char,	std::allocator<unsigned char>);
TmpUnserialize(CDataStream,	unsigned char,	secure_allocator<unsigned char>);
TmpUnserialize(CDataStream,	CAddress,		std::allocator<CAddress>);
TmpUnserialize(CDataStream,	CDiskTxPos,		std::allocator<CDiskTxPos>);
TmpUnserialize(CDataStream,	CInv,			std::allocator<CInv>);
TmpUnserialize(CDataStream,	CMasternode,	std::allocator<CMasternode>);
TmpUnserialize(CDataStream,	CMerkleTx,		std::allocator<CMerkleTx>);
TmpUnserialize(CDataStream,	CTransaction,	std::allocator<CTransaction>);
TmpUnserialize(CDataStream,	CTxIn,			std::allocator<CTxIn>);
TmpUnserialize(CDataStream,	CTxOut,			std::allocator<CTxOut>);
TmpUnserialize(CDataStream,	uint256,		std::allocator<uint256>);
		
template void Unserialize<CDataStream, std::pair<std::string, std::string>, std::allocator<std::pair<std::string, std::string>>>(
		CDataStream&, std::vector<std::pair<std::string, std::string>, std::allocator<std::pair<std::string, std::string>>>&, int, int);

TmpUnserialize(CAutoFile,	unsigned char,	std::allocator<unsigned char>);
TmpUnserialize(CAutoFile,	CDiskTxPos,		std::allocator<CDiskTxPos>);
TmpUnserialize(CAutoFile,	CTransaction,	std::allocator<CTransaction>);
TmpUnserialize(CAutoFile,	CTxIn,			std::allocator<CTxIn>);
TmpUnserialize(CAutoFile,	CTxOut,			std::allocator<CTxOut>);

// Serialize Script
template void Serialize<CDataStream>(CDataStream&, CScript const&, int, int);
template void Serialize<CAutoFile>(CAutoFile&, CScript const&, int, int);
template void Serialize<CHashWriter>(CHashWriter&, CScript const&, int, int);

// Unserialize Script
template void Unserialize<CDataStream>(CDataStream&, CScript&, int, int);
template void Unserialize<CAutoFile>(CAutoFile&, CScript&, int, int);

