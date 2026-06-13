#ifndef UINT160_H
#define UINT160_H

#include <cstdint>
#include <vector>

#include "uint/uint_base.h"

/** 160-bit unsigned integer */
class uint160 : public uint_base160
{
public:
	uint160();
	uint160(const uint_base160& b);
	uint160(uint64_t b);
	explicit uint160(const std::string& str);
	explicit uint160(const std::vector<unsigned char>& vch);

	uint160& operator=(const uint_base160& b);
	uint160& operator=(uint64_t b);
};

bool operator==(const uint160& a, uint64_t b);
bool operator!=(const uint160& a, uint64_t b);
const uint160 operator<<(const uint_base160& a, unsigned int shift);
const uint160 operator>>(const uint_base160& a, unsigned int shift);
const uint160 operator<<(const uint160& a, unsigned int shift);
const uint160 operator>>(const uint160& a, unsigned int shift);

const uint160 operator^(const uint_base160& a, const uint_base160& b);
const uint160 operator&(const uint_base160& a, const uint_base160& b);
const uint160 operator|(const uint_base160& a, const uint_base160& b);
const uint160 operator+(const uint_base160& a, const uint_base160& b);
const uint160 operator-(const uint_base160& a, const uint_base160& b);

bool operator<(const uint_base160& a, const uint160& b);
bool operator<=(const uint_base160& a, const uint160& b);
bool operator>(const uint_base160& a, const uint160& b);
bool operator>=(const uint_base160& a, const uint160& b);
bool operator==(const uint_base160& a, const uint160& b);
bool operator!=(const uint_base160& a, const uint160& b);
const uint160 operator^(const uint_base160& a, const uint160& b);
const uint160 operator&(const uint_base160& a, const uint160& b);
const uint160 operator|(const uint_base160& a, const uint160& b);
const uint160 operator+(const uint_base160& a, const uint160& b);
const uint160 operator-(const uint_base160& a, const uint160& b);

bool operator<(const uint160& a, const uint_base160& b);
bool operator<=(const uint160& a, const uint_base160& b);
bool operator>(const uint160& a, const uint_base160& b);
bool operator>=(const uint160& a, const uint_base160& b);
bool operator==(const uint160& a, const uint_base160& b);
bool operator!=(const uint160& a, const uint_base160& b);
const uint160 operator^(const uint160& a, const uint_base160& b);
const uint160 operator&(const uint160& a, const uint_base160& b);
const uint160 operator|(const uint160& a, const uint_base160& b);
const uint160 operator+(const uint160& a, const uint_base160& b);
const uint160 operator-(const uint160& a, const uint_base160& b);

bool operator<(const uint160& a, const uint160& b);
bool operator<=(const uint160& a, const uint160& b);
bool operator>(const uint160& a, const uint160& b);
bool operator>=(const uint160& a, const uint160& b);
bool operator==(const uint160& a, const uint160& b);
bool operator!=(const uint160& a, const uint160& b);
const uint160 operator^(const uint160& a, const uint160& b);
const uint160 operator&(const uint160& a, const uint160& b);
const uint160 operator|(const uint160& a, const uint160& b);
const uint160 operator+(const uint160& a, const uint160& b);
const uint160 operator-(const uint160& a, const uint160& b);

#endif // UINT160_H
