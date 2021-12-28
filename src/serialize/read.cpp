#include <ios>

#include "cdatastream.h"
#include "cautofile.h"
#include "cscriptcompressor.h"
#include "cvarint.h"
#include "cpubkey.h"
#include "main_const.h"
#include "serialize.h"
#include "cflatdata.h"
#include "ctxin.h"
#include "ctxout.h"
#include "ctxindex.h"
#include "cmasternode.h"
#include "caddress.h"
#include "caddrman.h"
#include "net/cbanentry.h"
#include "net/cservice.h"
#include "net/csubnet.h"
#include "cinv.h"
#include "uint/uint160.h"
#include "uint/uint256.h"
#include "smsg/stored.h"
#include "cwallettx.h"
#include "cwalletkey.h"
#include "cunsignedalert.h"
#include "cstealthkeymetadata.h"
#include "cstealthaddress.h"
#include "csporkmessage.h"
#include "cmasternodepaymentwinner.h"
#include "cmessageheader.h"
#include "cmasternodeman.h"
#include "cmasterkey.h"
#include "ckeypool.h"
#include "ckeymetadata.h"
#include "ckeyid.h"
#include "cdiskblockindex.h"
#include "cconsensusvote.h"
#include "cblocklocator.h"
#include "cblock.h"
#include "cbignum.h"
#include "calert.h"
#include "caccountingentry.h"
#include "caccount.h"
#include "caddrinfo.h"

#include "vector.h"

#include "serialize/read.h"

//
// GetSerializeSize
//
unsigned int GetSerializeSize(char a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(signed char a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(unsigned char a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(signed short a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(unsigned short a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(signed int a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(unsigned int a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(signed long a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(unsigned long a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(signed long long a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(unsigned long long a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(float a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(double a, int, int)
{
	return sizeof(a);
}

unsigned int GetSerializeSize(bool a, int, int)
{
	return sizeof(char);
}

template<typename T>
unsigned int GetSerializeSize(const T& a, long nType, int nVersion)
{
	return a.GetSerializeSize((int)nType, nVersion);
}

//
// Unserialize
//
template<typename Stream>
void Unserialize(Stream& s, char& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, signed char& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, unsigned char& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, signed short& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, unsigned short& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, signed int& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, unsigned int& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, signed long& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, unsigned long& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, signed long long& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, unsigned long long& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, float& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, double& a, int, int)
{
	READDATA(s, a);
}

template<typename Stream>
void Unserialize(Stream& s, bool& a, int, int)
{
	char f;
	
	READDATA(s, f);
	
	a = f;
}

template<typename Stream, typename T>
void Unserialize(Stream& is, T& a, long nType, int nVersion)
{
	a.Unserialize(is, (int)nType, nVersion);
}

template<typename Stream>
uint64_t ReadCompactSize(Stream& is)
{
    unsigned char chSize;
    uint64_t nSizeRet = 0;
    
	READDATA(is, chSize);
    
	if (chSize < 253)
    {
        nSizeRet = chSize;
    }
    else if (chSize == 253)
    {
        unsigned short xSize;
        
		READDATA(is, xSize);
        nSizeRet = xSize;
        
		if (nSizeRet < 253)
		{
            throw std::ios_base::failure("non-canonical ReadCompactSize()");
		}
    }
    else if (chSize == 254)
    {
        unsigned int xSize;
        
		READDATA(is, xSize);
        nSizeRet = xSize;
        
		if (nSizeRet < 0x10000u)
		{
            throw std::ios_base::failure("non-canonical ReadCompactSize()");
		}
    }
    else
    {
        uint64_t xSize;
        
		READDATA(is, xSize);
        nSizeRet = xSize;
        
		if (nSizeRet < 0x100000000ULL)
		{
            throw std::ios_base::failure("non-canonical ReadCompactSize()");
		}
    }
	
    if (nSizeRet > (uint64_t)MAX_MESSAGE_SIZE)
	{
        throw std::ios_base::failure("ReadCompactSize() : size too large");
	}
	
    return nSizeRet;
}



//
// Generate Templates
//

// GetSerializeSize
#define TmpGetSerializeSize(A)	template unsigned int GetSerializeSize<A>(A const&, long, int);

TmpGetSerializeSize(CAddress);
TmpGetSerializeSize(CAddrInfo);
TmpGetSerializeSize(CBlock);
TmpGetSerializeSize(CDiskTxPos);
TmpGetSerializeSize(CFlatData);
TmpGetSerializeSize(CMasternode);
TmpGetSerializeSize(CMerkleTx);
TmpGetSerializeSize(CNetAddr);
TmpGetSerializeSize(COutPoint);
TmpGetSerializeSize(CPubKey);
TmpGetSerializeSize(CScriptCompressor);
TmpGetSerializeSize(CService);
TmpGetSerializeSize(CTransaction);
TmpGetSerializeSize(CTxIn);
TmpGetSerializeSize(CTxOut);
TmpGetSerializeSize(CVarInt<int>);
TmpGetSerializeSize(CVarInt<long>);
TmpGetSerializeSize(CVarInt<unsigned int>);
TmpGetSerializeSize(uint256);

// Unserialize
#define TmpUnserialize(A, B)	template void Unserialize<A>(A&, B&, int, int);

TmpUnserialize(CDataStream, bool);
TmpUnserialize(CDataStream, int);
TmpUnserialize(CDataStream, long);
TmpUnserialize(CDataStream, long long);
TmpUnserialize(CDataStream, char);
TmpUnserialize(CDataStream, unsigned int);
TmpUnserialize(CDataStream, unsigned long);
TmpUnserialize(CDataStream, unsigned long long);
TmpUnserialize(CDataStream, unsigned char);
TmpUnserialize(CDataStream, unsigned short);

TmpUnserialize(CAutoFile, int)
TmpUnserialize(CAutoFile, long)
TmpUnserialize(CAutoFile, unsigned int)
TmpUnserialize(CAutoFile, unsigned char)

// Unserialize 2
#define TmpUnserialize2(A, B)	template void Unserialize<A, B>(A&, B&, long, int);

TmpUnserialize2(CDataStream, CAccount);
TmpUnserialize2(CDataStream, CAccountingEntry);
TmpUnserialize2(CDataStream, CAddress);
TmpUnserialize2(CDataStream, CAddrInfo);
TmpUnserialize2(CDataStream, CAddrMan);
TmpUnserialize2(CDataStream, CAlert);
TmpUnserialize2(CDataStream, CBanEntry);
TmpUnserialize2(CDataStream, CBigNum);
TmpUnserialize2(CDataStream, CBlock);
TmpUnserialize2(CDataStream, CBlockLocator);
TmpUnserialize2(CDataStream, CConsensusVote);
TmpUnserialize2(CDataStream, CDiskBlockIndex);
TmpUnserialize2(CDataStream, CDiskTxPos);
TmpUnserialize2(CDataStream, CFlatData);
TmpUnserialize2(CDataStream, CInv);
TmpUnserialize2(CDataStream, CKeyID);
TmpUnserialize2(CDataStream, CKeyMetadata);
TmpUnserialize2(CDataStream, CKeyPool);
TmpUnserialize2(CDataStream, CMasterKey);
TmpUnserialize2(CDataStream, CMasternode);
TmpUnserialize2(CDataStream, CMasternodeMan);
TmpUnserialize2(CDataStream, CMasternodePaymentWinner);
TmpUnserialize2(CDataStream, CMerkleTx);
TmpUnserialize2(CDataStream, CMessageHeader);
TmpUnserialize2(CDataStream, CNetAddr);
TmpUnserialize2(CDataStream, COutPoint);
TmpUnserialize2(CDataStream, CPubKey);
TmpUnserialize2(CDataStream, CScriptCompressor);
TmpUnserialize2(CDataStream, CService);
TmpUnserialize2(CDataStream, CSporkMessage);
TmpUnserialize2(CDataStream, CStealthAddress);
TmpUnserialize2(CDataStream, CStealthKeyMetadata);
TmpUnserialize2(CDataStream, CSubNet);
TmpUnserialize2(CDataStream, CTxIn);
TmpUnserialize2(CDataStream, CTxIndex);
TmpUnserialize2(CDataStream, CTxOut);
TmpUnserialize2(CDataStream, CTransaction);
TmpUnserialize2(CDataStream, CUnsignedAlert);
TmpUnserialize2(CDataStream, CWalletKey);
TmpUnserialize2(CDataStream, CWalletTx);
TmpUnserialize2(CDataStream, CVarInt<int>);
TmpUnserialize2(CDataStream, CVarInt<long>);
TmpUnserialize2(CDataStream, CVarInt<unsigned int>);
TmpUnserialize2(CDataStream, uint160);
TmpUnserialize2(CDataStream, uint256);
TmpUnserialize2(CDataStream, DigitalNote::SMSG::Stored);

TmpUnserialize2(CAutoFile, CBlock);
TmpUnserialize2(CAutoFile, CDiskTxPos);
TmpUnserialize2(CAutoFile, CFlatData);
TmpUnserialize2(CAutoFile, COutPoint);
TmpUnserialize2(CAutoFile, CScriptCompressor);
TmpUnserialize2(CAutoFile, CTransaction);
TmpUnserialize2(CAutoFile, CTxIn);
TmpUnserialize2(CAutoFile, CTxOut);
TmpUnserialize2(CAutoFile, CVarInt<long>);
TmpUnserialize2(CAutoFile, CVarInt<unsigned int>);
TmpUnserialize2(CAutoFile, uint256);

// ReadCompactSize
template unsigned long ReadCompactSize<CDataStream>(CDataStream&);
template unsigned long ReadCompactSize<CAutoFile>(CAutoFile&);

