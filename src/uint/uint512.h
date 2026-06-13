#ifndef UINT512_H
#define UINT512_H

#include <cstdint>
#include <string>
#include <vector>

#include "uint/uint_base.h"

class uint256;

/** 512-bit unsigned integer */
class uint512 : public uint_base512
{
public:
	uint512();
	uint512(const uint_base512& b);
	uint512(uint64_t b);
	explicit uint512(const std::string& str);
	explicit uint512(const std::vector<unsigned char>& vch);

	uint512& operator=(const uint_base512& b);
	uint512& operator=(uint64_t b);
	uint256 trim256() const;
};

bool operator==(const uint512& a, uint64_t b);
bool operator!=(const uint512& a, uint64_t b);
const uint512 operator<<(const uint_base512& a, unsigned int shift);
const uint512 operator>>(const uint_base512& a, unsigned int shift);
const uint512 operator<<(const uint512& a, unsigned int shift);
const uint512 operator>>(const uint512& a, unsigned int shift);

const uint512 operator^(const uint_base512& a, const uint_base512& b);
const uint512 operator&(const uint_base512& a, const uint_base512& b);
const uint512 operator|(const uint_base512& a, const uint_base512& b);
const uint512 operator+(const uint_base512& a, const uint_base512& b);
const uint512 operator-(const uint_base512& a, const uint_base512& b);

bool operator<(const uint_base512& a, const uint512& b);
bool operator<=(const uint_base512& a, const uint512& b);
bool operator>(const uint_base512& a, const uint512& b);
bool operator>=(const uint_base512& a, const uint512& b);
bool operator==(const uint_base512& a, const uint512& b);
bool operator!=(const uint_base512& a, const uint512& b);
const uint512 operator^(const uint_base512& a, const uint512& b);
const uint512 operator&(const uint_base512& a, const uint512& b);
const uint512 operator|(const uint_base512& a, const uint512& b);
const uint512 operator+(const uint_base512& a, const uint512& b);
const uint512 operator-(const uint_base512& a, const uint512& b);

bool operator<(const uint512& a, const uint_base512& b);
bool operator<=(const uint512& a, const uint_base512& b);
bool operator>(const uint512& a, const uint_base512& b);
bool operator>=(const uint512& a, const uint_base512& b);
bool operator==(const uint512& a, const uint_base512& b);
bool operator!=(const uint512& a, const uint_base512& b);
const uint512 operator^(const uint512& a, const uint_base512& b);
const uint512 operator&(const uint512& a, const uint_base512& b);
const uint512 operator|(const uint512& a, const uint_base512& b);
const uint512 operator+(const uint512& a, const uint_base512& b);
const uint512 operator-(const uint512& a, const uint_base512& b);

bool operator<(const uint512& a, const uint512& b);
bool operator<=(const uint512& a, const uint512& b);
bool operator>(const uint512& a, const uint512& b);
bool operator>=(const uint512& a, const uint512& b);
bool operator==(const uint512& a, const uint512& b);
bool operator!=(const uint512& a, const uint512& b);
const uint512 operator^(const uint512& a, const uint512& b);
const uint512 operator&(const uint512& a, const uint512& b);
const uint512 operator|(const uint512& a, const uint512& b);
const uint512 operator+(const uint512& a, const uint512& b);
const uint512 operator-(const uint512& a, const uint512& b);

#endif // UINT512_H
