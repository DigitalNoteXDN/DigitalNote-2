// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef UINT256_H
#define UINT256_H

#include <cstdint>
#include <vector>

#include "uint/uint_base.h"

/** 256-bit unsigned integer */
class uint256 : public uint_base256
{
public:
	uint256();
	uint256(const uint_base256& b);
	uint256(uint64_t b);
	explicit uint256(const std::string& str);
	explicit uint256(const std::vector<unsigned char>& vch);

	uint256& operator=(const uint_base256& b);
	uint256& operator=(uint64_t b);
};

bool operator==(const uint256& a, uint64_t b);
bool operator!=(const uint256& a, uint64_t b);
const uint256 operator<<(const uint_base256& a, unsigned int shift);
const uint256 operator>>(const uint_base256& a, unsigned int shift);
const uint256 operator<<(const uint256& a, unsigned int shift);
const uint256 operator>>(const uint256& a, unsigned int shift);

const uint256 operator^(const uint_base256& a, const uint_base256& b);
const uint256 operator&(const uint_base256& a, const uint_base256& b);
const uint256 operator|(const uint_base256& a, const uint_base256& b);
const uint256 operator+(const uint_base256& a, const uint_base256& b);
const uint256 operator-(const uint_base256& a, const uint_base256& b);

bool operator<(const uint_base256& a, const uint256& b);
bool operator<=(const uint_base256& a, const uint256& b);
bool operator>(const uint_base256& a, const uint256& b);
bool operator>=(const uint_base256& a, const uint256& b);
bool operator==(const uint_base256& a, const uint256& b);
bool operator!=(const uint_base256& a, const uint256& b);
const uint256 operator^(const uint_base256& a, const uint256& b);
const uint256 operator&(const uint_base256& a, const uint256& b);
const uint256 operator|(const uint_base256& a, const uint256& b);
const uint256 operator+(const uint_base256& a, const uint256& b);
const uint256 operator-(const uint_base256& a, const uint256& b);

bool operator<(const uint256& a, const uint_base256& b);
bool operator<=(const uint256& a, const uint_base256& b);
bool operator>(const uint256& a, const uint_base256& b);
bool operator>=(const uint256& a, const uint_base256& b);
bool operator==(const uint256& a, const uint_base256& b);
bool operator!=(const uint256& a, const uint_base256& b);
const uint256 operator^(const uint256& a, const uint_base256& b);
const uint256 operator&(const uint256& a, const uint_base256& b);
const uint256 operator|(const uint256& a, const uint_base256& b);
const uint256 operator+(const uint256& a, const uint_base256& b);
const uint256 operator-(const uint256& a, const uint_base256& b);

bool operator<(const uint256& a, const uint256& b);
bool operator<=(const uint256& a, const uint256& b);
bool operator>(const uint256& a, const uint256& b);
bool operator>=(const uint256& a, const uint256& b);
bool operator==(const uint256& a, const uint256& b);
bool operator!=(const uint256& a, const uint256& b);
const uint256 operator^(const uint256& a, const uint256& b);
const uint256 operator&(const uint256& a, const uint256& b);
const uint256 operator|(const uint256& a, const uint256& b);
const uint256 operator+(const uint256& a, const uint256& b);
const uint256 operator-(const uint256& a, const uint256& b);

#endif // UINT256_H
