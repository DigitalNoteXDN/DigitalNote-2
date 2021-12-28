#include <limits>
#include <algorithm>

#include "uint/uint256.h"
#include "cbignum_error.h"
#include "cbignum_ctx.h"
#include "util.h"
#include "cdatastream.h"
#include "serialize.h"

#include "cbignum.h"

CBigNum::CBigNum() : bn(BN_new())
{
   
}

CBigNum::CBigNum(const CBigNum& b) : CBigNum()
{
	if (!BN_copy(bn, b.bn))
	{
		BN_clear_free(bn);
		
		throw CBigNum_Error("CBigNum::CBigNum(const CBigNum&) : BN_copy failed");
	}
}

CBigNum& CBigNum::operator=(const CBigNum& b)
{
	if (!BN_copy(bn, b.bn))
	{
		throw CBigNum_Error("CBigNum::operator= : BN_copy failed");
	}
	
	return (*this);
}

CBigNum::~CBigNum()
{
	BN_clear_free(bn);
}

//CBigNum(char n) is not portable.  Use 'signed char' or 'unsigned char'.
CBigNum::CBigNum(signed char n) : CBigNum()
{
	if (n >= 0)
	{
		setulong(n);
	}
	else
	{
		setint64(n);
	}
}

CBigNum::CBigNum(short n) : CBigNum()
{
	if (n >= 0)
	{
		setulong(n);
	}
	else
	{
		setint64(n);
	}
}

CBigNum::CBigNum(int n) : CBigNum()
{
	if (n >= 0)
	{
		setulong(n);
	}
	else
	{
		setint64(n);
	}
}

CBigNum::CBigNum(long n) : CBigNum()
{
	if (n >= 0)
	{
		setulong(n);
	}
	else
	{
		setint64(n);
	}
}

CBigNum::CBigNum(long long n) : CBigNum()
{
	setint64(n);
}

CBigNum::CBigNum(unsigned char n) : CBigNum()
{
	setulong(n);
}

CBigNum::CBigNum(unsigned short n) : CBigNum()
{
	setulong(n);
}

CBigNum::CBigNum(unsigned int n) : CBigNum()
{
	setulong(n);
}

CBigNum::CBigNum(unsigned long n) : CBigNum()
{
	setulong(n);
}

CBigNum::CBigNum(unsigned long long n) : CBigNum()
{
	setuint64(n);
}

CBigNum::CBigNum(uint256 n) : CBigNum()
{
	setuint256(n);
}

CBigNum::CBigNum(const std::vector<unsigned char>& vch) : CBigNum()
{
	setvch(vch);
}

/** Generates a cryptographically secure random number between zero and range exclusive
* i.e. 0 < returned number < range
* @param range The upper bound on the number.
* @return
*/
CBigNum  CBigNum::randBignum(const CBigNum& range)
{
	CBigNum ret;
	
	if(!BN_rand_range(ret.bn, range.bn))
	{
		throw CBigNum_Error("CBigNum:rand element : BN_rand_range failed");
	}
	
	return ret;
}

/** Generates a cryptographically secure random k-bit number
* @param k The bit length of the number.
* @return
*/
CBigNum CBigNum::RandKBitBigum(const uint32_t k)
{
	CBigNum ret;
	
	if(!BN_rand(ret.bn, k, -1, 0))
	{
		throw CBigNum_Error("CBigNum:rand element : BN_rand failed");
	}
	
	return ret;
}

/**Returns the size in bits of the underlying bignum.
 *
 * @return the size
 */
int CBigNum::bitSize() const
{
	return  BN_num_bits(bn);
}

void CBigNum::setulong(unsigned long n)
{
	if (!BN_set_word(bn, n))
	{
		throw CBigNum_Error("CBigNum conversion from unsigned long : BN_set_word failed");
	}
}

unsigned long CBigNum::getulong() const
{
	return BN_get_word(bn);
}

unsigned int CBigNum::getuint() const
{
	return BN_get_word(bn);
}

int CBigNum::getint() const
{
	unsigned long n = BN_get_word(bn);
	
	if (!BN_is_negative(bn))
	{
		return (n > (unsigned long)std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : n);
	}
	else
	{
		return (n > (unsigned long)std::numeric_limits<int>::max() ? std::numeric_limits<int>::min() : -(int)n);
	}
}

void CBigNum::setint64(int64_t sn)
{
	unsigned char pch[sizeof(sn) + 6];
	unsigned char* p = pch + 4;
	bool fNegative;
	uint64_t n;

	if (sn < (int64_t)0)
	{
		// Since the minimum signed integer cannot be represented as positive so long as its type is signed, and it's not well-defined what happens if you make it unsigned before negating it, we instead increment the negative integer by 1, convert it, then increment the (now positive) unsigned integer by 1 to compensate
		n = -(sn + 1);
		++n;
		fNegative = true;
	}
	else
	{
		n = sn;
		fNegative = false;
	}

	bool fLeadingZeroes = true;
	for (int i = 0; i < 8; i++)
	{
		unsigned char c = (n >> 56) & 0xff;
		n <<= 8;
		
		if (fLeadingZeroes)
		{
			if (c == 0)
			{
				continue;
			}
			
			if (c & 0x80)
			{
				*p++ = (fNegative ? 0x80 : 0);
			}
			else if (fNegative)
			{
				c |= 0x80;
			}
			
			fLeadingZeroes = false;
		}
		*p++ = c;
	}
	
	unsigned int nSize = p - (pch + 4);
	pch[0] = (nSize >> 24) & 0xff;
	pch[1] = (nSize >> 16) & 0xff;
	pch[2] = (nSize >> 8) & 0xff;
	pch[3] = (nSize) & 0xff;
	
	BN_mpi2bn(pch, p - pch, bn);
}

uint64_t CBigNum::getuint64()
{
	unsigned int nSize = BN_bn2mpi(bn, NULL);
	
	if (nSize < 4)
	{
		return 0;
	}
	
	std::vector<unsigned char> vch(nSize);
	
	BN_bn2mpi(bn, &vch[0]);
	
	if (vch.size() > 4)
	{
		vch[4] &= 0x7f;
	}
	
	uint64_t n = 0;
	for (unsigned int i = 0, j = vch.size()-1; i < sizeof(n) && j >= 4; i++, j--)
	{
		((unsigned char*)&n)[i] = vch[j];
	}
	
	return n;
}

void CBigNum::setuint64(uint64_t n)
{
	unsigned char pch[sizeof(n) + 6];
	unsigned char* p = pch + 4;
	bool fLeadingZeroes = true;
	
	for (int i = 0; i < 8; i++)
	{
		unsigned char c = (n >> 56) & 0xff;
		n <<= 8;
		
		if (fLeadingZeroes)
		{
			if (c == 0)
			{
				continue;
			}
			
			if (c & 0x80)
			{
				*p++ = 0;
			}
			
			fLeadingZeroes = false;
		}
		
		*p++ = c;
	}
	
	unsigned int nSize = p - (pch + 4);
	pch[0] = (nSize >> 24) & 0xff;
	pch[1] = (nSize >> 16) & 0xff;
	pch[2] = (nSize >> 8) & 0xff;
	pch[3] = (nSize) & 0xff;
	BN_mpi2bn(pch, p - pch, bn);
}

void CBigNum::setuint256(uint256 n)
{
	unsigned char pch[sizeof(n) + 6];
	unsigned char* p = pch + 4;
	bool fLeadingZeroes = true;
	unsigned char* pbegin = (unsigned char*)&n;
	unsigned char* psrc = pbegin + sizeof(n);
	
	while (psrc != pbegin)
	{
		unsigned char c = *(--psrc);
		if (fLeadingZeroes)
		{
			if (c == 0)
			{
				continue;
			}
			
			if (c & 0x80)
			{
				*p++ = 0;
			}
			
			fLeadingZeroes = false;
		}
		
		*p++ = c;
	}
	
	unsigned int nSize = p - (pch + 4);
	pch[0] = (nSize >> 24) & 0xff;
	pch[1] = (nSize >> 16) & 0xff;
	pch[2] = (nSize >> 8) & 0xff;
	pch[3] = (nSize >> 0) & 0xff;
	
	BN_mpi2bn(pch, p - pch, bn);
}

uint256 CBigNum::getuint256() const
{
	unsigned int nSize = BN_bn2mpi(bn, NULL);
	
	if (nSize < 4)
	{
		return 0;
	}
	
	std::vector<unsigned char> vch(nSize);
	BN_bn2mpi(bn, &vch[0]);
	
	if (vch.size() > 4)
	{
		vch[4] &= 0x7f;
	}
	
	uint256 n = 0;
	for (unsigned int i = 0, j = vch.size()-1; i < sizeof(n) && j >= 4; i++, j--)
	{
		((unsigned char*)&n)[i] = vch[j];
	}
	
	return n;
}

void CBigNum::setvch(const std::vector<unsigned char>& vch)
{
	std::vector<unsigned char> vch2(vch.size() + 4);
	unsigned int nSize = vch.size();
	
	// BIGNUM's byte stream format expects 4 bytes of
	// big endian size data info at the front
	vch2[0] = (nSize >> 24) & 0xff;
	vch2[1] = (nSize >> 16) & 0xff;
	vch2[2] = (nSize >> 8) & 0xff;
	vch2[3] = (nSize >> 0) & 0xff;
	
	// swap data to big endian
	reverse_copy(vch.begin(), vch.end(), vch2.begin() + 4);
	
	BN_mpi2bn(&vch2[0], vch2.size(), bn);
}

std::vector<unsigned char> CBigNum::getvch() const
{
	unsigned int nSize = BN_bn2mpi(bn, NULL);
	
	if (nSize <= 4)
	{
		return std::vector<unsigned char>();
	}
	
	std::vector<unsigned char> vch(nSize);
	
	BN_bn2mpi(bn, &vch[0]);
	
	vch.erase(vch.begin(), vch.begin() + 4);
	reverse(vch.begin(), vch.end());
	
	return vch;
}

CBigNum& CBigNum::SetCompact(unsigned int nCompact)
{
	unsigned int nSize = nCompact >> 24;
	std::vector<unsigned char> vch(4 + nSize);
	
	vch[3] = nSize;
	
	if (nSize >= 1)
	{
		vch[4] = (nCompact >> 16) & 0xff;
	}
	
	if (nSize >= 2)
	{
		vch[5] = (nCompact >> 8) & 0xff;
	}
	
	if (nSize >= 3)
	{
		vch[6] = (nCompact >> 0) & 0xff;
	}
	
	BN_mpi2bn(&vch[0], vch.size(), bn);
	
	return *this;
}

unsigned int CBigNum::GetCompact() const
{
	unsigned int nSize = BN_bn2mpi(bn, NULL);
	std::vector<unsigned char> vch(nSize);
	
	nSize -= 4;
	
	BN_bn2mpi(bn, &vch[0]);
	
	unsigned int nCompact = nSize << 24;
	if (nSize >= 1)
	{
		nCompact |= (vch[4] << 16);
	}
	
	if (nSize >= 2)
	{
		nCompact |= (vch[5] << 8);
	}
	
	if (nSize >= 3)
	{
		nCompact |= (vch[6] << 0);
	}
	
	return nCompact;
}

void CBigNum::SetHex(const std::string& str)
{
	const char* psz = str.c_str();
	bool fNegative = false;
	int n;
	
	// skip 0x
	while (isspace(*psz))
	{
		psz++;
	}
	
	if (*psz == '-')
	{
		fNegative = true;
		psz++;
	}
	
	if (psz[0] == '0' && tolower(psz[1]) == 'x')
	{
		psz += 2;
	}
	
	while (isspace(*psz))
	{
		psz++;
	}
	
	// hex string to bignum
	*this = 0;
	
	while ((n = HexDigit(*psz)) != -1)
	{
		*this <<= 4;
		*this += n;
		++psz;
	}
	
	if (fNegative)
	{
		BN_set_negative(bn, 1);
	}
}

std::string CBigNum::ToString(int nBase) const
{
	CBigNum_CTX pctx;
	CBigNum bnBase = nBase;
	CBigNum bn0 = 0;
	std::string str;
	CBigNum bn = *this;
	BN_set_negative(bn.bn, false);
	CBigNum dv;
	CBigNum rem;
	
	if (BN_cmp(bn.bn, bn0.bn) == 0)
	{
		return "0";
	}
	
	while (BN_cmp(bn.bn, bn0.bn) > 0)
	{
		if (!BN_div(dv.bn, rem.bn, bn.bn, bnBase.bn, pctx))
		{
			throw CBigNum_Error("CBigNum::ToString() : BN_div failed");
		}
		
		bn = dv;
		unsigned int c = rem.getulong();
		str += "0123456789abcdef"[c];
	}
	
	if (BN_is_negative(this->bn))
	{
		str += "-";
	}
	
	reverse(str.begin(), str.end());
	
	return str;
}

std::string CBigNum::GetHex() const
{
	return ToString(16);
}

unsigned int CBigNum::GetSerializeSize(int nType, int nVersion) const
{
	return ::GetSerializeSize(getvch(), nType, nVersion);
}

template<typename Stream>
void CBigNum::Serialize(Stream& s, int nType, int nVersion) const
{
	::Serialize(s, getvch(), nType, nVersion);
}

template<typename Stream>
void CBigNum::Unserialize(Stream& s, int nType, int nVersion)
{
	std::vector<unsigned char> vch;
	
	::Unserialize(s, vch, nType, nVersion);
	
	setvch(vch);
}

template void CBigNum::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CBigNum::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

/**
* exponentiation with an int. this^e
* @param e the exponent as an int
* @return
*/
CBigNum CBigNum::pow(const int e) const
{
	return this->pow(CBigNum(e));
}

/**
 * exponentiation this^e
 * @param e the exponent
 * @return
 */
CBigNum CBigNum::pow(const CBigNum& e) const
{
	CBigNum_CTX pctx;
	CBigNum ret;
	
	if (!BN_exp(ret.bn, bn, e.bn, pctx))
	{
		throw CBigNum_Error("CBigNum::pow : BN_exp failed");
	}
	
	return ret;
}

/**
 * modular multiplication: (this * b) mod m
 * @param b operand
 * @param m modulus
 */
CBigNum CBigNum::mul_mod(const CBigNum& b, const CBigNum& m) const
{
	CBigNum_CTX pctx;
	CBigNum ret;
	
	if (!BN_mod_mul(ret.bn, bn, b.bn, m.bn, pctx))
	{
		throw CBigNum_Error("CBigNum::mul_mod : BN_mod_mul failed");
	}
	
	return ret;
}

/**
 * modular exponentiation: this^e mod n
 * @param e exponent
 * @param m modulus
 */
CBigNum CBigNum::pow_mod(const CBigNum& e, const CBigNum& m) const
{
	CBigNum_CTX pctx;
	CBigNum ret;
	
	if(BN_is_negative(e.bn))
	{
		// g^-x = (g^-1)^x
		CBigNum inv = this->inverse(m);
		CBigNum posE(e);
		BN_set_negative(posE.bn, !BN_is_negative(e.bn));
		
		if (!BN_mod_exp(ret.bn, inv.bn, posE.bn, m.bn, pctx))
		{
			throw CBigNum_Error("CBigNum::pow_mod: BN_mod_exp failed on negative exponent");
		}
	}
	else
	{
		if (!BN_mod_exp(ret.bn, bn, e.bn, m.bn, pctx))
		{
			throw CBigNum_Error("CBigNum::pow_mod : BN_mod_exp failed");
		}
	}
	
	return ret;
}

/**
* Calculates the inverse of this element mod m.
* i.e. i such this*i = 1 mod m
* @param m the modu
* @return the inverse
*/
CBigNum CBigNum::inverse(const CBigNum& m) const
{
	CBigNum_CTX pctx;
	CBigNum ret;
	
	if (!BN_mod_inverse(ret.bn, bn, m.bn, pctx))
	{
		throw CBigNum_Error("CBigNum::inverse*= :BN_mod_inverse");
	}
	
	return ret;
}

/**
 * Generates a random (safe) prime of numBits bits
 * @param numBits the number of bits
 * @param safe true for a safe prime
 * @return the prime
 */
CBigNum CBigNum::generatePrime(const unsigned int numBits, bool safe)
{
	CBigNum ret;
	
	if(!BN_generate_prime_ex(ret.bn, numBits, (safe == true), NULL, NULL, NULL))
	{
		throw CBigNum_Error("CBigNum::generatePrime*= :BN_generate_prime_ex");
	}
	
	return ret;
}

/**
 * Calculates the greatest common divisor (GCD) of two numbers.
 * @param m the second element
 * @return the GCD
 */
CBigNum CBigNum::gcd(const CBigNum& b) const
{
	CBigNum_CTX pctx;
	CBigNum ret;
	
	if (!BN_gcd(ret.bn, bn, b.bn, pctx))
	{
		throw CBigNum_Error("CBigNum::gcd*= :BN_gcd");
	}
	
	return ret;
}

/**
* Miller-Rabin primality test on this element
* @param checks: optional, the number of Miller-Rabin tests to run
* default causes error rate of 2^-80.
* @return true if prime
*/
bool CBigNum::isPrime(const int checks) const
{
	CBigNum_CTX pctx;
	
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
	int ret = BN_is_prime_ex(bn, checks, pctx, NULL);
#else
	int ret = BN_is_prime(bn, checks, NULL, pctx, NULL);
#endif

	if(ret < 0)
	{
		throw CBigNum_Error("CBigNum::isPrime :BN_is_prime");
	}
	
	return ret;
}

bool CBigNum::isOne() const
{
	return BN_is_one(bn);
}

bool CBigNum::operator!() const
{
	return BN_is_zero(bn);
}

CBigNum& CBigNum::operator+=(const CBigNum& b)
{
	if (!BN_add(bn, bn, b.bn))
	{
		throw CBigNum_Error("CBigNum::operator+= : BN_add failed");
	}
	
	return *this;
}

CBigNum& CBigNum::operator-=(const CBigNum& b)
{
	if (!BN_sub(bn, bn, b.bn))
	{
		throw CBigNum_Error("CBigNum::operator-= : BN_sub failed");
	}
	
	return *this;
}

CBigNum& CBigNum::operator*=(const CBigNum& b)
{
	CBigNum_CTX pctx;
	
	if (!BN_mul(bn, bn, b.bn, pctx))
	{
		throw CBigNum_Error("CBigNum::operator*= : BN_mul failed");
	}
	
	return *this;
}

CBigNum& CBigNum::operator/=(const CBigNum& b)
{
	CBigNum_CTX pctx;
	
	if (!BN_div(bn, NULL, bn, b.bn, pctx))
	{
		throw CBigNum_Error("CBigNum::operator/= : BN_div failed");
	}
	
	return *this;
}

CBigNum& CBigNum::operator%=(const CBigNum& b)
{
	CBigNum_CTX pctx;
	
	if (!BN_div(NULL, bn, bn, b.bn, pctx))
	{
		throw CBigNum_Error("CBigNum::operator%= : BN_div failed");
	}
	
	return *this;
}

CBigNum& CBigNum::operator<<=(unsigned int shift)
{
	if (!BN_lshift(bn, bn, shift))
	{
		throw CBigNum_Error("CBigNum:operator<<= : BN_lshift failed");
	}
	
	return *this;
}

CBigNum& CBigNum::operator>>=(unsigned int shift)
{
	// Note: BN_rshift segfaults on 64-bit if 2^shift is greater than the number
	//   if built on ubuntu 9.04 or 9.10, probably depends on version of OpenSSL
	CBigNum a = 1;
	
	a <<= shift;
	
	if (BN_cmp(a.bn, bn) > 0)
	{
		*this = 0;
		return *this;
	}

	if (!BN_rshift(bn, bn, shift))
	{
		throw CBigNum_Error("CBigNum:operator>>= : BN_rshift failed");
	}
	
	return *this;
}

CBigNum& CBigNum::operator++()
{
	// prefix operator
	if (!BN_add(bn, bn, BN_value_one()))
	{
		throw CBigNum_Error("CBigNum::operator++ : BN_add failed");
	}
	
	return *this;
}

const CBigNum CBigNum::operator++(int)
{
	// postfix operator
	const CBigNum ret = *this;
	
	++(*this);
	
	return ret;
}

CBigNum& CBigNum::operator--()
{
	// prefix operator
	CBigNum r;
	
	if (!BN_sub(r.bn, bn, BN_value_one()))
	{
		throw CBigNum_Error("CBigNum::operator-- : BN_sub failed");
	}
	
	*this = r;
	
	return *this;
}

const CBigNum CBigNum::operator--(int)
{
	// postfix operator
	const CBigNum ret = *this;
	
	--(*this);
	
	return ret;
}

const BIGNUM* CBigNum::to_bignum() const
{
   return bn;
}

BIGNUM* CBigNum::to_bignum()
{
   return bn;
}

const CBigNum operator+(const CBigNum& a, const CBigNum& b)
{
    CBigNum r;
	
    if (!BN_add(r.bn, a.bn, b.bn))
	{
        throw CBigNum_Error("CBigNum::operator+ : BN_add failed");
    }
	
	return r;
}

const CBigNum operator-(const CBigNum& a, const CBigNum& b)
{
    CBigNum r;
    
	if (!BN_sub(r.bn, a.bn, b.bn))
	{
        throw CBigNum_Error("CBigNum::operator- : BN_sub failed");
	}
	
    return r;
}

const CBigNum operator-(const CBigNum& a)
{
    CBigNum r(a);
	
    BN_set_negative(r.bn, !BN_is_negative(r.bn));
    
	return r;
}

const CBigNum operator*(const CBigNum& a, const CBigNum& b)
{
    CBigNum_CTX pctx;
    CBigNum r;
	
    if (!BN_mul(r.bn, a.bn, b.bn, pctx))
	{
        throw CBigNum_Error("CBigNum::operator* : BN_mul failed");
	}
	
    return r;
}

const CBigNum operator/(const CBigNum& a, const CBigNum& b)
{
    CBigNum_CTX pctx;
    CBigNum r;
	
    if (!BN_div(r.bn, NULL, a.bn, b.bn, pctx))
    {
		throw CBigNum_Error("CBigNum::operator/ : BN_div failed");
	}
	
    return r;
}

const CBigNum operator%(const CBigNum& a, const CBigNum& b)
{
    CBigNum_CTX pctx;
    CBigNum r;
	
    if (!BN_nnmod(r.bn, a.bn, b.bn, pctx))
    {
		throw CBigNum_Error("CBigNum::operator% : BN_div failed");
    }
	
	return r;
}

const CBigNum operator<<(const CBigNum& a, unsigned int shift)
{
    CBigNum r;
	
    if (!BN_lshift(r.bn, a.bn, shift))
    {
		throw CBigNum_Error("CBigNum:operator<< : BN_lshift failed");
	}
	
    return r;
}

const CBigNum operator>>(const CBigNum& a, unsigned int shift)
{
    CBigNum r = a;
	
    r >>= shift;
    
	return r;
}

bool operator==(const CBigNum& a, const CBigNum& b)
{
	return (BN_cmp(a.bn, b.bn) == 0);
}

bool operator!=(const CBigNum& a, const CBigNum& b)
{
	return (BN_cmp(a.bn, b.bn) != 0);
}

bool operator<=(const CBigNum& a, const CBigNum& b)
{
	return (BN_cmp(a.bn, b.bn) <= 0);
}

bool operator>=(const CBigNum& a, const CBigNum& b)
{
	return (BN_cmp(a.bn, b.bn) >= 0);
}

bool operator<(const CBigNum& a, const CBigNum& b)
{
	return (BN_cmp(a.bn, b.bn) < 0);
}

bool operator>(const CBigNum& a, const CBigNum& b)
{
	return (BN_cmp(a.bn, b.bn) > 0);
}

std::ostream& operator<<(std::ostream &strm, const CBigNum &b)
{
	return strm << b.ToString(10);
}

