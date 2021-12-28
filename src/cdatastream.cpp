#include <ios>
#include <cassert>
#include <cstring>

#include "serialize.h"
#include "cmessageheader.h"
#include "cautofile.h"
#include "types/mapvalue_t.h"
#include "caddress.h"
#include "types/banmap_t.h"
#include "net/csubnet.h"
#include "net/cbanentry.h"
#include "uint/uint256.h"
#include "caddrman.h"
#include "smsg/stored.h"
#include "ckeyid.h"
#include "cpubkey.h"
#include "cmasternodepaymentwinner.h"
#include "cmasternodeman.h"
#include "cscript.h"
#include "net/cservice.h"
#include "ctxout.h"
#include "ctransaction.h"
#include "cmnenginequeue.h"
#include "uint/uint160.h"
#include "uint/uint256.h"
#include "csporkmessage.h"
#include "cconsensusvote.h"
#include "cblock.h"
#include "cunsignedalert.h"
#include "cbignum.h"
#include "cdiskblockindex.h"
#include "ctxindex.h"
#include "ckeypool.h"
#include "cstealthkeymetadata.h"
#include "ckeymetadata.h"
#include "types/ckeyingmaterial.h"
#include "cmasterkey.h"
#include "cwalletkey.h"
#include "caccountingentry.h"
#include "cwallettx.h"
#include "cstealthaddress.h"
#include "cinv.h"
#include "cblocklocator.h"
#include "calert.h"
#include "caccount.h"
#include "cflatdata.h"
#include "cvarint.h"

#include "cdatastream.h"

CDataStream::CDataStream(int nTypeIn, int nVersionIn)
{
	Init(nTypeIn, nVersionIn);
}

CDataStream::CDataStream(CDataStream::const_iterator pbegin, CDataStream::const_iterator pend, int nTypeIn, int nVersionIn) : vch(pbegin, pend)
{
	Init(nTypeIn, nVersionIn);
}

#if !defined(_MSC_VER) || _MSC_VER >= 1300
CDataStream::CDataStream(const char* pbegin, const char* pend, int nTypeIn, int nVersionIn) : vch(pbegin, pend)
{
	Init(nTypeIn, nVersionIn);
}
#endif

CDataStream::CDataStream(const vector_type& vchIn, int nTypeIn, int nVersionIn) : vch(vchIn.begin(), vchIn.end())
{
	Init(nTypeIn, nVersionIn);
}

CDataStream::CDataStream(const std::vector<char>& vchIn, int nTypeIn, int nVersionIn) : vch(vchIn.begin(), vchIn.end())
{
	Init(nTypeIn, nVersionIn);
}

CDataStream::CDataStream(const std::vector<unsigned char>& vchIn, int nTypeIn, int nVersionIn) : vch((char*)&vchIn.begin()[0], (char*)&vchIn.end()[0])
{
	Init(nTypeIn, nVersionIn);
}

void CDataStream::Init(int nTypeIn, int nVersionIn)
{
	nReadPos = 0;
	nType = nTypeIn;
	nVersion = nVersionIn;
	state = 0;
	exceptmask = std::ios::badbit | std::ios::failbit;
}

CDataStream& CDataStream::operator+=(const CDataStream& b)
{
	vch.insert(vch.end(), b.begin(), b.end());
	
	return *this;
}

CDataStream operator+(const CDataStream& a, const CDataStream& b)
{
	CDataStream ret = a;
	
	ret += b;
	
	return (ret);
}

std::string CDataStream::str() const
{
	return (std::string(begin(), end()));
}

//
// Vector subset
//
CDataStream::const_iterator CDataStream::begin() const
{
	return vch.begin() + nReadPos;
}

CDataStream::iterator CDataStream::begin()
{
	return vch.begin() + nReadPos;
}

CDataStream::const_iterator CDataStream::end() const
{
	return vch.end();
}

CDataStream::iterator CDataStream::end()
{
	return vch.end();
}

CDataStream::size_type CDataStream::size() const
{
	return vch.size() - nReadPos;
}

bool CDataStream::empty() const
{
	return vch.size() == nReadPos;
}

void CDataStream::resize(CDataStream::size_type n, value_type c)
{
	vch.resize(n + nReadPos, c);
}

void CDataStream::reserve(CDataStream::size_type n)
{
	vch.reserve(n + nReadPos);
}

CDataStream::const_reference CDataStream::operator[](CDataStream::size_type pos) const
{
	return vch[pos + nReadPos];
}

CDataStream::reference CDataStream::operator[](CDataStream::size_type pos)
{
	return vch[pos + nReadPos];
}

void CDataStream::clear()
{
	vch.clear();
	
	nReadPos = 0;
}

CDataStream::iterator CDataStream::insert(CDataStream::iterator it, const char& x)
{
	return vch.insert(it, x);
}

void CDataStream::insert(CDataStream::iterator it, CDataStream::size_type n, const char& x)
{
	vch.insert(it, n, x);
}

void CDataStream::insert(CDataStream::iterator it, std::vector<char>::const_iterator first, std::vector<char>::const_iterator last)
{
	assert(last - first >= 0);
	
	if (it == vch.begin() + nReadPos && (unsigned int)(last - first) <= nReadPos)
	{
		// special case for inserting at the front when there's room
		nReadPos -= (last - first);
		
		memcpy(&vch[nReadPos], &first[0], last - first);
	}
	else
	{
		vch.insert(it, first, last);
	}
}

#if !defined(_MSC_VER) || _MSC_VER >= 1300
void CDataStream::insert(CDataStream::iterator it, const char* first, const char* last)
{
	assert(last - first >= 0);
	
	if (it == vch.begin() + nReadPos && (unsigned int)(last - first) <= nReadPos)
	{
		// special case for inserting at the front when there's room
		nReadPos -= (last - first);
		
		memcpy(&vch[nReadPos], &first[0], last - first);
	}
	else
	{
		vch.insert(it, first, last);
	}
}
#endif

CDataStream::iterator CDataStream::erase(CDataStream::iterator it)
{
	if (it == vch.begin() + nReadPos)
	{
		// special case for erasing from the front
		if (++nReadPos >= vch.size())
		{
			// whenever we reach the end, we take the opportunity to clear the buffer
			nReadPos = 0;
			
			return vch.erase(vch.begin(), vch.end());
		}
		
		return vch.begin() + nReadPos;
	}
	else
	{
		return vch.erase(it);
	}
}

CDataStream::iterator CDataStream::erase(CDataStream::iterator first, CDataStream::iterator last)
{
	if (first == vch.begin() + nReadPos)
	{
		// special case for erasing from the front
		if (last == vch.end())
		{
			nReadPos = 0;
			
			return vch.erase(vch.begin(), vch.end());
		}
		else
		{
			nReadPos = (last - vch.begin());
			
			return last;
		}
	}
	else
	{
		return vch.erase(first, last);
	}
}

inline void CDataStream::Compact()
{
	vch.erase(vch.begin(), vch.begin() + nReadPos);
	
	nReadPos = 0;
}

bool CDataStream::Rewind(CDataStream::size_type n)
{
	// Rewind by n characters if the buffer hasn't been compacted yet
	if (n > nReadPos)
	{
		return false;
	}
	
	nReadPos -= n;
	
	return true;
}

//
// Stream subset
//
void CDataStream::setstate(short bits, const char* psz)
{
	state |= bits;
	
	if (state & exceptmask)
	{
		throw std::ios_base::failure(psz);
	}
}

bool CDataStream::eof() const
{
	return size() == 0;
}

bool CDataStream::fail() const
{
	return state & (std::ios::badbit | std::ios::failbit);
}

bool CDataStream::good() const
{
	return !eof() && (state == 0);
}

void CDataStream::clear(short n)
{
	state = n;
}  // name conflict with vector clear()

short CDataStream::exceptions()
{
	return exceptmask;
}

short CDataStream::exceptions(short mask)
{
	short prev = exceptmask;
	
	exceptmask = mask;
	
	setstate(0, "CDataStream");
	
	return prev;
}

CDataStream* CDataStream::rdbuf()
{
	return this;
}

int CDataStream::in_avail()
{
	return size();
}

void CDataStream::SetType(int n)
{
	nType = n;
}

int CDataStream::GetType()
{
	return nType;
}

void CDataStream::SetVersion(int n)
{
	nVersion = n;
}

int CDataStream::GetVersion()
{
	return nVersion;
}

void CDataStream::ReadVersion()
{
	*this >> nVersion;
}

void CDataStream::WriteVersion()
{
	*this << nVersion;
}

CDataStream& CDataStream::read(char* pch, size_t nSize)
{
	// Read from the beginning of the buffer
	unsigned int nReadPosNext = nReadPos + nSize;
	
	if (nReadPosNext >= vch.size())
	{
		if (nReadPosNext > vch.size())
		{
			setstate(std::ios::failbit, "CDataStream::read() : end of data");
			
			memset(pch, 0, nSize);
			
			nSize = vch.size() - nReadPos;
		}
		
		memcpy(pch, &vch[nReadPos], nSize);
		
		nReadPos = 0;
		vch.clear();
		
		return (*this);
	}
	
	memcpy(pch, &vch[nReadPos], nSize);
	nReadPos = nReadPosNext;
	
	return (*this);
}

CDataStream& CDataStream::ignore(int nSize)
{
	// Ignore from the beginning of the buffer
	assert(nSize >= 0);
	
	unsigned int nReadPosNext = nReadPos + nSize;
	
	if (nReadPosNext >= vch.size())
	{
		if (nReadPosNext > vch.size())
		{
			setstate(std::ios::failbit, "CDataStream::ignore() : end of data");
		}
		
		nReadPos = 0;
		vch.clear();
		
		return (*this);
	}
	
	nReadPos = nReadPosNext;
	
	return (*this);
}

CDataStream& CDataStream::write(const char* pch, size_t nSize)
{
	// Write to the end of the buffer
	vch.insert(vch.end(), pch, pch + nSize);
	
	return (*this);
}

template<typename Stream>
void CDataStream::Serialize(Stream& s, int nType, int nVersion) const
{
	// Special case: stream << stream concatenates like stream += stream
	if (!vch.empty())
	{
		s.write((char*)&vch[0], vch.size() * sizeof(vch[0]));
	}
}

template void CDataStream::Serialize<CDataStream>(CDataStream&, int, int) const;
template void CDataStream::Serialize<CAutoFile>(CAutoFile&, int, int) const;

template<typename T>
unsigned int CDataStream::GetSerializeSize(const T& obj)
{
	// Tells the size of the object if serialized to this stream
	return ::GetSerializeSize(obj, nType, nVersion);
}

template<typename T>
CDataStream& CDataStream::operator<<(const T& obj)
{
	// Serialize to this stream
	::Serialize(*this, obj, nType, nVersion);
	
	return (*this);
}

template CDataStream& CDataStream::operator<< <int>(int const&);
template CDataStream& CDataStream::operator<< <unsigned int>(unsigned int const&);
template CDataStream& CDataStream::operator<< <long>(long const&);
template CDataStream& CDataStream::operator<< <unsigned long>(unsigned long const&);
template CDataStream& CDataStream::operator<< <long long>(long long const&);
template CDataStream& CDataStream::operator<< <unsigned long long>(unsigned long long const&);
template CDataStream& CDataStream::operator<< <bool>(bool const&);
template CDataStream& CDataStream::operator<< <char>(char const&);
template CDataStream& CDataStream::operator<< <unsigned char>(unsigned char const&);
template CDataStream& CDataStream::operator<< <std::string>(std::string const&);

template CDataStream& CDataStream::operator<< <std::pair<std::string, long> >(std::pair<std::string, long> const&);
template CDataStream& CDataStream::operator<< <std::pair<std::string, long long> >(std::pair<std::string, long long> const&);
template CDataStream& CDataStream::operator<< <std::pair<std::string, unsigned int> >(std::pair<std::string, unsigned int> const&);
template CDataStream& CDataStream::operator<< <std::pair<std::string, std::string>>(std::pair<std::string, std::string> const&);
template CDataStream& CDataStream::operator<< <std::pair<std::string, CKeyID> >(std::pair<std::string, CKeyID> const&);
template CDataStream& CDataStream::operator<< <std::pair<std::string, CPubKey> >(std::pair<std::string, CPubKey> const&);
template CDataStream& CDataStream::operator<< <std::pair<std::string, CScript> >(std::pair<std::string, CScript> const&);
template CDataStream& CDataStream::operator<< <std::pair<std::string, ec_point>>(std::pair<std::string, ec_point> const&);
template CDataStream& CDataStream::operator<< <std::pair<std::string, uint160> >(std::pair<std::string, uint160> const&);
template CDataStream& CDataStream::operator<< <std::pair<std::string, uint256> >(std::pair<std::string, uint256> const&);
template CDataStream& CDataStream::operator<< <std::pair<CPrivKey, uint256>>(std::pair<CPrivKey, uint256> const&);

template CDataStream& CDataStream::operator<< <std::vector<unsigned char>>(std::vector<unsigned char> const&);
template CDataStream& CDataStream::operator<< <std::vector<CAddress>>(std::vector<CAddress> const&);
template CDataStream& CDataStream::operator<< <std::vector<CBlock>>(std::vector<CBlock> const&);
template CDataStream& CDataStream::operator<< <std::vector<CInv>>(std::vector<CInv> const&);
template CDataStream& CDataStream::operator<< <std::vector<CTxIn>>(std::vector<CTxIn> const&);
template CDataStream& CDataStream::operator<< <std::vector<uint256> >(std::vector<uint256> const&);

template CDataStream& CDataStream::operator<< <mapValue_t>(const mapValue_t&);
template CDataStream& CDataStream::operator<< <CFlatData>(CFlatData const&);
template CDataStream& CDataStream::operator<< <CVarInt<unsigned int> >(CVarInt<unsigned int> const&);
template CDataStream& CDataStream::operator<< <CAddress>(CAddress const&);
template CDataStream& CDataStream::operator<< <CMessageHeader>(CMessageHeader const&);
template CDataStream& CDataStream::operator<< <uint256>(uint256 const&);
template CDataStream& CDataStream::operator<< <banmap_t>(banmap_t const&);
template CDataStream& CDataStream::operator<< <CAddrMan>(CAddrMan const&);
template CDataStream& CDataStream::operator<< <DigitalNote::SMSG::Stored>(DigitalNote::SMSG::Stored const&);
template CDataStream& CDataStream::operator<< <CKeyID>(CKeyID const&);
template CDataStream& CDataStream::operator<< <CPubKey>(CPubKey const&);
template CDataStream& CDataStream::operator<< <CMasternodePaymentWinner>(CMasternodePaymentWinner const&);
template CDataStream& CDataStream::operator<< <CMasternodeMan>(CMasternodeMan const&);
template CDataStream& CDataStream::operator<< <CTxIn>(CTxIn const&);
template CDataStream& CDataStream::operator<< <CScript>(CScript const&);
template CDataStream& CDataStream::operator<< <CService>(CService const&);
template CDataStream& CDataStream::operator<< <std::vector<CTxOut>>(std::vector<CTxOut> const&);
template CDataStream& CDataStream::operator<< <CTransaction>(CTransaction const&);
template CDataStream& CDataStream::operator<< <CMNengineQueue>(CMNengineQueue const&);
template CDataStream& CDataStream::operator<< <CSporkMessage>(CSporkMessage const&);
template CDataStream& CDataStream::operator<< <CBlock>(CBlock const&);
template CDataStream& CDataStream::operator<< <CUnsignedAlert>(CUnsignedAlert const&);
template CDataStream& CDataStream::operator<< <CBigNum>(CBigNum const&);
template CDataStream& CDataStream::operator<< <CDiskBlockIndex>(CDiskBlockIndex const&);
template CDataStream& CDataStream::operator<< <CTxIndex>(CTxIndex const&);
template CDataStream& CDataStream::operator<< <CDataStream>(CDataStream const&);
template CDataStream& CDataStream::operator<< <CBlockLocator>(CBlockLocator const&);
template CDataStream& CDataStream::operator<< <CConsensusVote>(CConsensusVote const&);
template CDataStream& CDataStream::operator<< <CAlert>(CAlert const&);
template CDataStream& CDataStream::operator<< <CAccountingEntry>(CAccountingEntry const&);
template CDataStream& CDataStream::operator<< <boost::tuples::tuple<std::string, std::string, unsigned long> >(boost::tuples::tuple<std::string, std::string, unsigned long> const&);
template CDataStream& CDataStream::operator<< <boost::tuples::tuple<std::string, std::string, unsigned long long> >(boost::tuples::tuple<std::string, std::string, unsigned long long> const&);
template CDataStream& CDataStream::operator<< <CWalletTx>(CWalletTx const&);
template CDataStream& CDataStream::operator<< <CStealthKeyMetadata>(CStealthKeyMetadata const&);
template CDataStream& CDataStream::operator<< <CStealthAddress>(CStealthAddress const&);
template CDataStream& CDataStream::operator<< <CKeyMetadata>(CKeyMetadata const&);
template CDataStream& CDataStream::operator<< <CAccount>(CAccount const&);
template CDataStream& CDataStream::operator<< <CMasterKey>(CMasterKey const&);
template CDataStream& CDataStream::operator<< <CKeyPool>(CKeyPool const&);

template<typename T>
CDataStream& CDataStream::operator>>(T& obj)
{
	// Unserialize from this stream
	::Unserialize(*this, obj, nType, nVersion);
	
	return (*this);
}

template CDataStream& CDataStream::operator>><int>(int&);
template CDataStream& CDataStream::operator>><unsigned int>(unsigned int&);
template CDataStream& CDataStream::operator>><long>(long&);
template CDataStream& CDataStream::operator>><unsigned long>(unsigned long&);
template CDataStream& CDataStream::operator>><long long>(long long&);
template CDataStream& CDataStream::operator>><unsigned long long>(unsigned long long&);
template CDataStream& CDataStream::operator>><bool>(bool&);
template CDataStream& CDataStream::operator>><char>(char&);

template CDataStream& CDataStream::operator>><std::string>(std::string&);
template CDataStream& CDataStream::operator>><std::vector<unsigned char>>(std::vector<unsigned char>&);
template CDataStream& CDataStream::operator>><std::vector<uint256>>(std::vector<uint256>&);
template CDataStream& CDataStream::operator>><std::vector<CInv>>(std::vector<CInv>&);
template CDataStream& CDataStream::operator>><std::vector<CAddress>>(std::vector<CAddress>&);

template CDataStream& CDataStream::operator>><banmap_t>(banmap_t&);
template CDataStream& CDataStream::operator>><CAccount>(CAccount&);
template CDataStream& CDataStream::operator>><CAccountingEntry>(CAccountingEntry&);
template CDataStream& CDataStream::operator>><CAddress>(CAddress&);
template CDataStream& CDataStream::operator>><CAddrMan>(CAddrMan&);
template CDataStream& CDataStream::operator>><CAlert>(CAlert&);
template CDataStream& CDataStream::operator>><CBigNum>(CBigNum&);
template CDataStream& CDataStream::operator>><CBlock>(CBlock&);
template CDataStream& CDataStream::operator>><CBlockLocator>(CBlockLocator&);
template CDataStream& CDataStream::operator>><CConsensusVote>(CConsensusVote&);
template CDataStream& CDataStream::operator>><CDiskBlockIndex>(CDiskBlockIndex&);
template CDataStream& CDataStream::operator>><CFlatData>(CFlatData&);
template CDataStream& CDataStream::operator>><CKeyingMaterial>(CKeyingMaterial&);
template CDataStream& CDataStream::operator>><CKeyID>(CKeyID&);
template CDataStream& CDataStream::operator>><CKeyMetadata>(CKeyMetadata&);
template CDataStream& CDataStream::operator>><CKeyPool>(CKeyPool&);
template CDataStream& CDataStream::operator>><CMasterKey>(CMasterKey&);
template CDataStream& CDataStream::operator>><CMasternodeMan>(CMasternodeMan&);
template CDataStream& CDataStream::operator>><CMessageHeader>(CMessageHeader&);
template CDataStream& CDataStream::operator>><CMasternodePaymentWinner>(CMasternodePaymentWinner&);
template CDataStream& CDataStream::operator>><CPubKey>(CPubKey&);
template CDataStream& CDataStream::operator>><CScript>(CScript&);
template CDataStream& CDataStream::operator>><CService>(CService&);
template CDataStream& CDataStream::operator>><CSporkMessage>(CSporkMessage&);
template CDataStream& CDataStream::operator>><CStealthAddress>(CStealthAddress&);
template CDataStream& CDataStream::operator>><CStealthKeyMetadata>(CStealthKeyMetadata&);
template CDataStream& CDataStream::operator>><CTransaction>(CTransaction&);
template CDataStream& CDataStream::operator>><CTxIn>(CTxIn&);
template CDataStream& CDataStream::operator>><CTxIndex>(CTxIndex&);
template CDataStream& CDataStream::operator>><CUnsignedAlert>(CUnsignedAlert&);
template CDataStream& CDataStream::operator>><CVarInt<unsigned int> >(CVarInt<unsigned int>&);
template CDataStream& CDataStream::operator>><CWalletKey>(CWalletKey&);
template CDataStream& CDataStream::operator>><CWalletTx>(CWalletTx&);
template CDataStream& CDataStream::operator>><DigitalNote::SMSG::Stored>(DigitalNote::SMSG::Stored&);
template CDataStream& CDataStream::operator>><mapValue_t>(mapValue_t&);
template CDataStream& CDataStream::operator>><uint160>(uint160&);
template CDataStream& CDataStream::operator>><uint256>(uint256&);

void CDataStream::GetAndClear(CSerializeData &data)
{
	data.insert(data.end(), begin(), end());
	
	clear();
}

