#ifndef CBIGNUM_H
#define CBIGNUM_H

#include <vector>
#include <openssl/bn.h>

#include "version.h"

class CBigNum;
class uint256;

const CBigNum operator+(const CBigNum& a, const CBigNum& b);
const CBigNum operator-(const CBigNum& a, const CBigNum& b);
const CBigNum operator-(const CBigNum& a);
const CBigNum operator*(const CBigNum& a, const CBigNum& b);
const CBigNum operator/(const CBigNum& a, const CBigNum& b);
const CBigNum operator%(const CBigNum& a, const CBigNum& b);
const CBigNum operator<<(const CBigNum& a, unsigned int shift);
const CBigNum operator>>(const CBigNum& a, unsigned int shift);
bool operator==(const CBigNum& a, const CBigNum& b);
bool operator!=(const CBigNum& a, const CBigNum& b);
bool operator<=(const CBigNum& a, const CBigNum& b);
bool operator>=(const CBigNum& a, const CBigNum& b);
bool operator<(const CBigNum& a, const CBigNum& b);
bool operator>(const CBigNum& a, const CBigNum& b);
std::ostream& operator<<(std::ostream &strm, const CBigNum &b);

/** C++ wrapper for BIGNUM (OpenSSL bignum) */
class CBigNum
{
protected:
	BIGNUM* bn;

public:
	CBigNum();
	CBigNum(const CBigNum& b);
	CBigNum& operator=(const CBigNum& b);
	~CBigNum();
	//CBigNum(char n) is not portable.  Use 'signed char' or 'unsigned char'.
	CBigNum(signed char n);
	CBigNum(short n);
	CBigNum(int n);
	CBigNum(long n);
	CBigNum(long long n);
	CBigNum(unsigned char n);
	CBigNum(unsigned short n);
	CBigNum(unsigned int n);
	CBigNum(unsigned long n);
	CBigNum(unsigned long long n);
	explicit CBigNum(uint256 n);
	explicit CBigNum(const std::vector<unsigned char>& vch);
	/** Generates a cryptographically secure random number between zero and range exclusive
	* i.e. 0 < returned number < range
	* @param range The upper bound on the number.
	* @return
	*/
	static CBigNum  randBignum(const CBigNum& range);
	/** Generates a cryptographically secure random k-bit number
	* @param k The bit length of the number.
	* @return
	*/
	static CBigNum RandKBitBigum(const uint32_t k);
	/**Returns the size in bits of the underlying bignum.
	 *
	 * @return the size
	 */
	int bitSize() const;
	void setulong(unsigned long n);
	unsigned long getulong() const;
	unsigned int getuint() const;
	int getint() const;
	void setint64(int64_t sn);
	uint64_t getuint64();
	void setuint64(uint64_t n);
	void setuint256(uint256 n);
	uint256 getuint256() const;
	void setvch(const std::vector<unsigned char>& vch);
	std::vector<unsigned char> getvch() const;
	CBigNum& SetCompact(unsigned int nCompact);
	unsigned int GetCompact() const;
	void SetHex(const std::string& str);
	std::string ToString(int nBase=10) const;
	std::string GetHex() const;
	unsigned int GetSerializeSize(int nType=0, int nVersion=PROTOCOL_VERSION) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType=0, int nVersion=PROTOCOL_VERSION) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType=0, int nVersion=PROTOCOL_VERSION);
	/**
	* exponentiation with an int. this^e
	* @param e the exponent as an int
	* @return
	*/
	CBigNum pow(const int e) const;
	/**
	 * exponentiation this^e
	 * @param e the exponent
	 * @return
	 */
	CBigNum pow(const CBigNum& e) const;
	/**
	 * modular multiplication: (this * b) mod m
	 * @param b operand
	 * @param m modulus
	 */
	CBigNum mul_mod(const CBigNum& b, const CBigNum& m) const;
	/**
	 * modular exponentiation: this^e mod n
	 * @param e exponent
	 * @param m modulus
	 */
	CBigNum pow_mod(const CBigNum& e, const CBigNum& m) const;
	/**
	* Calculates the inverse of this element mod m.
	* i.e. i such this*i = 1 mod m
	* @param m the modu
	* @return the inverse
	*/
	CBigNum inverse(const CBigNum& m) const;
	/**
	 * Generates a random (safe) prime of numBits bits
	 * @param numBits the number of bits
	 * @param safe true for a safe prime
	 * @return the prime
	 */
	static CBigNum generatePrime(const unsigned int numBits, bool safe = false);
	/**
	 * Calculates the greatest common divisor (GCD) of two numbers.
	 * @param m the second element
	 * @return the GCD
	 */
	CBigNum gcd( const CBigNum& b) const;
	/**
	* Miller-Rabin primality test on this element
	* @param checks: optional, the number of Miller-Rabin tests to run
	* default causes error rate of 2^-80.
	* @return true if prime
	*/
	bool isPrime(const int checks=BN_prime_checks) const;
	bool isOne() const;
	bool operator!() const;
	CBigNum& operator+=(const CBigNum& b);
	CBigNum& operator-=(const CBigNum& b);
	CBigNum& operator*=(const CBigNum& b);
	CBigNum& operator/=(const CBigNum& b);
	CBigNum& operator%=(const CBigNum& b);
	CBigNum& operator<<=(unsigned int shift);
	CBigNum& operator>>=(unsigned int shift);
	CBigNum& operator++();
	const CBigNum operator++(int);
	CBigNum& operator--();
	const CBigNum operator--(int);
	const BIGNUM* to_bignum() const;
	BIGNUM* to_bignum();

	friend const CBigNum operator+(const CBigNum& a, const CBigNum& b);
	friend const CBigNum operator-(const CBigNum& a, const CBigNum& b);
	friend const CBigNum operator-(const CBigNum& a);
	friend const CBigNum operator*(const CBigNum& a, const CBigNum& b);
	friend const CBigNum operator/(const CBigNum& a, const CBigNum& b);
	friend const CBigNum operator%(const CBigNum& a, const CBigNum& b);
	friend const CBigNum operator<<(const CBigNum& a, unsigned int shift);
	friend const CBigNum operator>>(const CBigNum& a, unsigned int shift);
	friend bool operator==(const CBigNum& a, const CBigNum& b);
	friend bool operator!=(const CBigNum& a, const CBigNum& b);
	friend bool operator<=(const CBigNum& a, const CBigNum& b);
	friend bool operator>=(const CBigNum& a, const CBigNum& b);
	friend bool operator<(const CBigNum& a, const CBigNum& b);
	friend bool operator>(const CBigNum& a, const CBigNum& b);
};

#endif // CBIGNUM_H
