#include "compat.h"

#include <cstring>

#include "uint/uint256.h"
#include "serialize.h"
#include "util.h"
#include "hash.h"
#include "chashwriter.h"
#include "cautofile.h"
#include "cdatastream.h"

#include "uint_base.h"

template<unsigned int BITS>
bool uint_base<BITS>::operator!() const
{
	for (int i = 0; i < WIDTH; i++)
		if (pn[i] != 0)
			return false;
	return true;
}

template<unsigned int BITS>
const uint_base<BITS> uint_base<BITS>::operator~() const
{
	uint_base ret;
	for (int i = 0; i < WIDTH; i++)
		ret.pn[i] = ~pn[i];
	return ret;
}

template<unsigned int BITS>
const uint_base<BITS> uint_base<BITS>::operator-() const
{
	uint_base ret;
	for (int i = 0; i < WIDTH; i++)
		ret.pn[i] = ~pn[i];
	ret++;
	return ret;
}

template<unsigned int BITS>
double uint_base<BITS>::getdouble() const
{
	double ret = 0.0;
	double fact = 1.0;
	for (int i = 0; i < WIDTH; i++) {
		ret += fact * pn[i];
		fact *= 4294967296.0;
	}
	return ret;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator=(uint64_t b)
{
	pn[0] = (unsigned int)b;
	pn[1] = (unsigned int)(b >> 32);
	for (int i = 2; i < WIDTH; i++)
		pn[i] = 0;
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator^=(const uint_base<BITS>& b)
{
	for (int i = 0; i < WIDTH; i++)
		pn[i] ^= b.pn[i];
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator&=(const uint_base<BITS>& b)
{
	for (int i = 0; i < WIDTH; i++)
		pn[i] &= b.pn[i];
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator|=(const uint_base<BITS>& b)
{
	for (int i = 0; i < WIDTH; i++)
		pn[i] |= b.pn[i];
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator^=(uint64_t b)
{
	pn[0] ^= (unsigned int)b;
	pn[1] ^= (unsigned int)(b >> 32);
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator|=(uint64_t b)
{
	pn[0] |= (unsigned int)b;
	pn[1] |= (unsigned int)(b >> 32);
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator<<=(unsigned int shift)
{
	uint_base a(*this);
	for (int i = 0; i < WIDTH; i++)
		pn[i] = 0;
	int k = shift / 32;
	shift = shift % 32;
	for (int i = 0; i < WIDTH; i++)
	{
		if (i+k+1 < WIDTH && shift != 0)
			pn[i+k+1] |= (a.pn[i] >> (32-shift));
		if (i+k < WIDTH)
			pn[i+k] |= (a.pn[i] << shift);
	}
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator>>=(unsigned int shift)
{
	uint_base a(*this);
	for (int i = 0; i < WIDTH; i++)
		pn[i] = 0;
	int k = shift / 32;
	shift = shift % 32;
	for (int i = 0; i < WIDTH; i++)
	{
		if (i-k-1 >= 0 && shift != 0)
			pn[i-k-1] |= (a.pn[i] << (32-shift));
		if (i-k >= 0)
			pn[i-k] |= (a.pn[i] >> shift);
	}
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator+=(const uint_base<BITS>& b)
{
	uint64_t carry = 0;
	for (int i = 0; i < WIDTH; i++)
	{
		uint64_t n = carry + pn[i] + b.pn[i];
		pn[i] = n & 0xffffffff;
		carry = n >> 32;
	}
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator-=(const uint_base<BITS>& b)
{
	*this += -b;
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator+=(uint64_t b64)
{
	uint_base b;
	b = b64;
	*this += b;
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator-=(uint64_t b64)
{
	uint_base b;
	b = b64;
	*this += -b;
	return *this;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator++()
{
	// prefix operator
	int i = 0;
	while (++pn[i] == 0 && i < WIDTH-1)
		i++;
	return *this;
}

template<unsigned int BITS>
const uint_base<BITS> uint_base<BITS>::operator++(int)
{
	// postfix operator
	const uint_base ret = *this;
	++(*this);
	return ret;
}

template<unsigned int BITS>
uint_base<BITS>& uint_base<BITS>::operator--()
{
	// prefix operator
	int i = 0;
	while (--pn[i] == -1 && i < WIDTH-1)
		i++;
	return *this;
}

template<unsigned int BITS>
const uint_base<BITS> uint_base<BITS>::operator--(int)
{
	// postfix operator
	const uint_base ret = *this;
	--(*this);
	return ret;
}

template<unsigned int BITS>
inline bool operator<(const uint_base<BITS>& a, const uint_base<BITS>& b)
{
	for (int i = uint_base<BITS>::WIDTH-1; i >= 0; i--)
	{
		if (a.pn[i] < b.pn[i])
			return true;
		else if (a.pn[i] > b.pn[i])
			return false;
	}
	return false;
}

template<unsigned int BITS>
inline bool operator<=(const uint_base<BITS>& a, const uint_base<BITS>& b)
{
	for (int i = uint_base<BITS>::WIDTH-1; i >= 0; i--)
	{
		if (a.pn[i] < b.pn[i])
			return true;
		else if (a.pn[i] > b.pn[i])
			return false;
	}
	return true;
}

template<unsigned int BITS>
inline bool operator>(const uint_base<BITS>& a, const uint_base<BITS>& b)
{
	for (int i = uint_base<BITS>::WIDTH-1; i >= 0; i--)
	{
		if (a.pn[i] > b.pn[i])
			return true;
		else if (a.pn[i] < b.pn[i])
			return false;
	}
	return false;
}

template<unsigned int BITS>
inline bool operator>=(const uint_base<BITS>& a, const uint_base<BITS>& b)
{
	for (int i = uint_base<BITS>::WIDTH-1; i >= 0; i--)
	{
		if (a.pn[i] > b.pn[i])
			return true;
		else if (a.pn[i] < b.pn[i])
			return false;
	}
	return true;
}

template<unsigned int BITS>
inline bool operator==(const uint_base<BITS>& a, const uint_base<BITS>& b)
{
	for (int i = 0; i < uint_base<BITS>::WIDTH; i++)
		if (a.pn[i] != b.pn[i])
			return false;
	return true;
}

template<unsigned int BITS>
inline bool operator==(const uint_base<BITS>& a, uint64_t b)
{
	if (a.pn[0] != (unsigned int)b)
		return false;
	if (a.pn[1] != (unsigned int)(b >> 32))
		return false;
	for (int i = 2; i < uint_base<BITS>::WIDTH; i++)
		if (a.pn[i] != 0)
			return false;
	return true;
}

template<unsigned int BITS>
inline bool operator!=(const uint_base<BITS>& a, const uint_base<BITS>& b)
{
	return (!(a == b));
}

template<unsigned int BITS>
inline bool operator!=(const uint_base<BITS>& a, uint64_t b)
{
	return (!(a == b));
}

template<unsigned int BITS>
std::string uint_base<BITS>::GetHex() const
{
	char psz[sizeof(pn)*2 + 1];
	for (unsigned int i = 0; i < sizeof(pn); i++)
		sprintf(psz + i*2, "%02x", ((unsigned char*)pn)[sizeof(pn) - i - 1]);
	return std::string(psz, psz + sizeof(pn)*2);
}

template<unsigned int BITS>
void uint_base<BITS>::SetHex(const char* psz)
{
	memset(pn,0,sizeof(pn));

	// skip leading spaces
	while (isspace(*psz))
		psz++;

	// skip 0x
	if (psz[0] == '0' && tolower(psz[1]) == 'x')
		psz += 2;

	// hex string to uint
	const char* pbegin = psz;
	while (::HexDigit(*psz) != -1)
		psz++;
	psz--;
	unsigned char* p1 = (unsigned char*)pn;
	unsigned char* pend = p1 + WIDTH * 4;
	while (psz >= pbegin && p1 < pend)
	{
		*p1 = ::HexDigit(*psz--);
		if (psz >= pbegin)
		{
			*p1 |= ((unsigned char)::HexDigit(*psz--) << 4);
			p1++;
		}
	}
}

template<unsigned int BITS>
void uint_base<BITS>::SetHex(const std::string& str)
{
	SetHex(str.c_str());
}

template<unsigned int BITS>
std::string uint_base<BITS>::ToString() const
{
	return (GetHex());
}

template<unsigned int BITS>
unsigned char* uint_base<BITS>::begin()
{
	return (unsigned char*)&pn[0];
}

template<unsigned int BITS>
unsigned char* uint_base<BITS>::end()
{
	return (unsigned char*)&pn[WIDTH];
}

template<unsigned int BITS>
const unsigned char* uint_base<BITS>::begin() const
{
return (unsigned char*)&pn[0];
}

template<unsigned int BITS>
const unsigned char* uint_base<BITS>::end() const
{
return (unsigned char*)&pn[WIDTH];
}

template<unsigned int BITS>
unsigned int uint_base<BITS>::size()
{
	return sizeof(pn);
}

template<unsigned int BITS>
uint64_t uint_base<BITS>::Get64(int n) const
{
	return pn[2*n] | (uint64_t)pn[2*n+1] << 32;
}

template<unsigned int BITS>
unsigned int uint_base<BITS>::GetSerializeSize(int nType, int nVersion) const
{
	return sizeof(pn);
}

template<unsigned int BITS>
template<typename Stream>
void uint_base<BITS>::Serialize(Stream& s, int nType, int nVersion) const
{
	s.write((char*)pn, sizeof(pn));
};

template<unsigned int BITS>
template<typename Stream>
void uint_base<BITS>::Unserialize(Stream& s, int nType, int nVersion)
{
	s.read((char*)pn, sizeof(pn));
};

/**
	Solution to force to compile the template code for different BITS/Stream template types.
	
	Reference:
		https://stackoverflow.com/questions/8752837/undefined-reference-to-template-class-constructor
*/
template class uint_base<160>;
template bool operator<  <160>(const uint_base<160>& a, const uint_base<160>& b);
template bool operator<= <160>(const uint_base<160>& a, const uint_base<160>& b);
template bool operator>  <160>(const uint_base<160>& a, const uint_base<160>& b);
template bool operator>= <160>(const uint_base<160>& a, const uint_base<160>& b);
template bool operator== <160>(const uint_base<160>& a, const uint_base<160>& b);
template bool operator== <160>(const uint_base<160>& a, uint64_t b);
template bool operator!= <160>(const uint_base<160>& a, const uint_base<160>& b);
template bool operator!= <160>(const uint_base<160>& a, uint64_t b);
template void uint_base<160>::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void uint_base<160>::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void uint_base<160>::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void uint_base<160>::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
template void uint_base<160>::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;

template class uint_base<256>;
template bool operator<  <256>(const uint_base<256>& a, const uint_base<256>& b);
template bool operator<= <256>(const uint_base<256>& a, const uint_base<256>& b);
template bool operator>  <256>(const uint_base<256>& a, const uint_base<256>& b);
template bool operator>= <256>(const uint_base<256>& a, const uint_base<256>& b);
template bool operator== <256>(const uint_base<256>& a, const uint_base<256>& b);
template bool operator== <256>(const uint_base<256>& a, uint64_t b);
template bool operator!= <256>(const uint_base<256>& a, const uint_base<256>& b);
template bool operator!= <256>(const uint_base<256>& a, uint64_t b);
template void uint_base<256>::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void uint_base<256>::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void uint_base<256>::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void uint_base<256>::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
template void uint_base<256>::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;

template class uint_base<512>;
template bool operator<  <512>(const uint_base<512>& a, const uint_base<512>& b);
template bool operator<= <512>(const uint_base<512>& a, const uint_base<512>& b);
template bool operator>  <512>(const uint_base<512>& a, const uint_base<512>& b);
template bool operator>= <512>(const uint_base<512>& a, const uint_base<512>& b);
template bool operator== <512>(const uint_base<512>& a, const uint_base<512>& b);
template bool operator== <512>(const uint_base<512>& a, uint64_t b);
template bool operator!= <512>(const uint_base<512>& a, const uint_base<512>& b);
template bool operator!= <512>(const uint_base<512>& a, uint64_t b);
template void uint_base<512>::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void uint_base<512>::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void uint_base<512>::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void uint_base<512>::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
template void uint_base<512>::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;
