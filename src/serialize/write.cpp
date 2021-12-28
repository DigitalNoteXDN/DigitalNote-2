#include <limits>

#include "cdatastream.h"
#include "chashwriter.h"
#include "csizecomputer.h"
#include "cautofile.h"
#include "cscriptcompressor.h"
#include "cvarint.h"
#include "cpubkey.h"
#include "ctxin.h"
#include "ctxout.h"
#include "cflatdata.h"
#include "ctransaction.h"
#include "net/cservice.h"
#include "serialize.h"
#include "cmasternode.h"
#include "ckeypool.h"
#include "cmasterkey.h"
#include "caccount.h"
#include "ckeymetadata.h"
#include "cstealthaddress.h"
#include "cstealthkeymetadata.h"
#include "cwallettx.h"
#include "caccountingentry.h"
#include "calert.h"
#include "cconsensusvote.h"
#include "cblocklocator.h"
#include "ctxindex.h"
#include "cdiskblockindex.h"
#include "cbignum.h"
#include "cunsignedalert.h"
#include "cblock.h"
#include "csporkmessage.h"
#include "cmnenginequeue.h"
#include "ctransaction.h"
#include "cmasternodeman.h"
#include "cmasternodepaymentwinner.h"
#include "ckeyid.h"
#include "smsg/stored.h"
#include "caddrman.h"
#include "net/cbanentry.h"
#include "net/csubnet.h"
#include "uint/uint160.h"
#include "uint/uint256.h"
#include "cmessageheader.h"
#include "caddress.h"
#include "cinv.h"
#include "caddrinfo.h"

#include "serialize/write.h"

template<typename Stream>
void Serialize(Stream& s, char a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, signed char a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, unsigned char a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, signed short a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, unsigned short a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, signed int a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, unsigned int a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, signed long a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, unsigned long a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, signed long long a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, unsigned long long a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, float a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, double a, int, int)
{
	WRITEDATA(s, a);
}

template<typename Stream>
void Serialize(Stream& s, bool a, int, int)
{
	char f = a;
	
	WRITEDATA(s, f);
}

template<typename Stream, typename T>
void Serialize(Stream& os, const T& a, long nType, int nVersion)
{
	a.Serialize(os, (int)nType, nVersion);
}

template<typename Stream>
void WriteCompactSize(Stream& os, uint64_t nSize)
{
    if (nSize < 253)
    {
        unsigned char chSize = nSize;
		
        WRITEDATA(os, chSize);
    }
    else if (nSize <= std::numeric_limits<unsigned short>::max())
    {
        unsigned char chSize = 253;
        unsigned short xSize = nSize;
        
		WRITEDATA(os, chSize);
        WRITEDATA(os, xSize);
    }
    else if (nSize <= std::numeric_limits<unsigned int>::max())
    {
        unsigned char chSize = 254;
        unsigned int xSize = nSize;
		
        WRITEDATA(os, chSize);
        WRITEDATA(os, xSize);
    }
    else
    {
        unsigned char chSize = 255;
        uint64_t xSize = nSize;
		
        WRITEDATA(os, chSize);
        WRITEDATA(os, xSize);
    }
	
    return;
}



//
// Generate Templates
//

// Serialize
#define TmpSerialize(A, B)	template void Serialize<A>(A&, B, int, int);

TmpSerialize(CDataStream,	bool);
TmpSerialize(CDataStream,	int);
TmpSerialize(CDataStream,	long);
TmpSerialize(CDataStream,	long long);
TmpSerialize(CDataStream,	char);
TmpSerialize(CDataStream,	unsigned int);
TmpSerialize(CDataStream,	unsigned long);
TmpSerialize(CDataStream,	unsigned long long);
TmpSerialize(CDataStream,	unsigned char);
TmpSerialize(CDataStream,	unsigned short);

TmpSerialize(CAutoFile,	int);
TmpSerialize(CAutoFile,	long);
TmpSerialize(CAutoFile,	long long);
TmpSerialize(CAutoFile,	unsigned int);
TmpSerialize(CAutoFile,	unsigned char);

TmpSerialize(CHashWriter,	int);
TmpSerialize(CHashWriter,	long);
TmpSerialize(CHashWriter,	long long);
TmpSerialize(CHashWriter,	unsigned int);
TmpSerialize(CHashWriter,	unsigned char);

TmpSerialize(CSizeComputer,	int);
TmpSerialize(CSizeComputer,	long);
TmpSerialize(CSizeComputer,	long long);

// Serialize 2
#define TmpSerialize2(A, B)	template void Serialize<A, B>(A&, const B&, long, int);

TmpSerialize2(CDataStream, CAccount);
TmpSerialize2(CDataStream, CAccountingEntry);
TmpSerialize2(CDataStream, CAddrInfo);
TmpSerialize2(CDataStream, CAddrMan);
TmpSerialize2(CDataStream, CAddress);
TmpSerialize2(CDataStream, CAlert);
TmpSerialize2(CDataStream, CBanEntry);
TmpSerialize2(CDataStream, CBlock);
TmpSerialize2(CDataStream, CBlockLocator);
TmpSerialize2(CDataStream, CBigNum);
TmpSerialize2(CDataStream, CConsensusVote);
TmpSerialize2(CDataStream, CDataStream);
TmpSerialize2(CDataStream, CDiskBlockIndex);
TmpSerialize2(CDataStream, CDiskTxPos);
TmpSerialize2(CDataStream, CFlatData);
TmpSerialize2(CDataStream, CInv);
TmpSerialize2(CDataStream, CKeyID);
TmpSerialize2(CDataStream, CKeyMetadata);
TmpSerialize2(CDataStream, CKeyPool);
TmpSerialize2(CDataStream, CMasterKey);
TmpSerialize2(CDataStream, CMasternode);
TmpSerialize2(CDataStream, CMasternodeMan);
TmpSerialize2(CDataStream, CMasternodePaymentWinner);
TmpSerialize2(CDataStream, CMerkleTx);
TmpSerialize2(CDataStream, CMessageHeader);
TmpSerialize2(CDataStream, CMNengineQueue);
TmpSerialize2(CDataStream, CNetAddr);
TmpSerialize2(CDataStream, COutPoint);
TmpSerialize2(CDataStream, CPubKey);
TmpSerialize2(CDataStream, CScriptCompressor);
TmpSerialize2(CDataStream, CService);
TmpSerialize2(CDataStream, CSporkMessage);
TmpSerialize2(CDataStream, CStealthAddress);
TmpSerialize2(CDataStream, CStealthKeyMetadata);
TmpSerialize2(CDataStream, CSubNet);
TmpSerialize2(CDataStream, CTransaction);
TmpSerialize2(CDataStream, CTxIn);
TmpSerialize2(CDataStream, CTxIndex);
TmpSerialize2(CDataStream, CTxOut);
TmpSerialize2(CDataStream, CUnsignedAlert);
TmpSerialize2(CDataStream, CVarInt<int>);
TmpSerialize2(CDataStream, CVarInt<long>);
TmpSerialize2(CDataStream, CVarInt<long long>);
TmpSerialize2(CDataStream, CVarInt<unsigned int>);
TmpSerialize2(CDataStream, CWalletTx);
TmpSerialize2(CDataStream, DigitalNote::SMSG::Stored);
TmpSerialize2(CDataStream, uint160);
TmpSerialize2(CDataStream, uint256);

TmpSerialize2(CAutoFile, CBlock);
TmpSerialize2(CAutoFile, CDataStream);
TmpSerialize2(CAutoFile, CDiskTxPos);
TmpSerialize2(CAutoFile, CFlatData);
TmpSerialize2(CAutoFile, COutPoint);
TmpSerialize2(CAutoFile, CScriptCompressor);
TmpSerialize2(CAutoFile, CTransaction);
TmpSerialize2(CAutoFile, CTxIn);
TmpSerialize2(CAutoFile, CTxOut);
TmpSerialize2(CAutoFile, uint256);
TmpSerialize2(CAutoFile, CVarInt<long>);
TmpSerialize2(CAutoFile, CVarInt<long long>);
TmpSerialize2(CAutoFile, CVarInt<unsigned int>);

TmpSerialize2(CHashWriter, CDiskTxPos);
TmpSerialize2(CHashWriter, CFlatData);
TmpSerialize2(CHashWriter, COutPoint);
TmpSerialize2(CHashWriter, CScriptCompressor);
TmpSerialize2(CHashWriter, CTransaction);
TmpSerialize2(CHashWriter, CTxIn);
TmpSerialize2(CHashWriter, CTxOut);
TmpSerialize2(CHashWriter, CVarInt<long>);
TmpSerialize2(CHashWriter, CVarInt<long long>);
TmpSerialize2(CHashWriter, CVarInt<unsigned int>);

// WriteCompactSize
template void WriteCompactSize<CDataStream>(CDataStream&, uint64_t);
template void WriteCompactSize<CAutoFile>(CAutoFile&, uint64_t);
template void WriteCompactSize<CHashWriter>(CHashWriter&, uint64_t);
template void WriteCompactSize<CSizeComputer>(CSizeComputer&, uint64_t);

