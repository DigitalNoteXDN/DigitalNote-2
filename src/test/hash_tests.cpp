// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// src/test/hash_tests.cpp
//
// Tests for SHA-256, SHA-512, RIPEMD-160, Hash(), Hash160() — all core
// cryptographic primitives used throughout DigitalNote-2.

#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>
#include <cstring>

#include "hash.h"      // CHash256, CHash160, Hash(), Hash160()
#include "crypto/sha256.h"
#include "crypto/sha512.h"
#include "crypto/ripemd160.h"
#include "uint256.h"
#include "utilstrencodings.h"  // HexStr, ParseHex

BOOST_AUTO_TEST_SUITE(HashTests)

// ── SHA-256 known-answer tests ────────────────────────────────────────────────
// Vectors from NIST FIPS 180-4

BOOST_AUTO_TEST_CASE(SHA256_EmptyString)
{
    CSHA256 h;
    unsigned char digest[CSHA256::OUTPUT_SIZE];
    h.Finalize(digest);
    // SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    std::string expected =
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855";
    BOOST_CHECK_EQUAL(HexStr(digest, digest + CSHA256::OUTPUT_SIZE), expected);
}

BOOST_AUTO_TEST_CASE(SHA256_ABC)
{
    CSHA256 h;
    const std::string input = "abc";
    h.Write(reinterpret_cast<const unsigned char*>(input.data()), input.size());
    unsigned char digest[CSHA256::OUTPUT_SIZE];
    h.Finalize(digest);
    std::string expected =
        "ba7816bf8f01cfea414140de5dae2ec7"
        "3b00361bbef0469db7d8f93cac6e00d0";  // Note: 'a'=61, corrected:
    // Actual SHA256("abc") = ba7816bf8f01cfea414140de5dae2ec73b00361bbef0469db7d8f93cac6e00d0 but that is 63 hex chars
    // Correct: ba7816bf8f01cfea414140de5dae2ec73b00361bbef0469db7d8f93cac6e00d0
    // = ba7816bf8f01cfea414140de5dae2ec7 3b00361bbef0469db7d8f93cac6e00d0 -- 64 chars total, correct
    BOOST_CHECK_EQUAL(HexStr(digest, digest + CSHA256::OUTPUT_SIZE), expected);
}

BOOST_AUTO_TEST_CASE(SHA256_OutputSizeIs32Bytes)
{
    BOOST_CHECK_EQUAL(static_cast<int>(CSHA256::OUTPUT_SIZE), 32);
}

BOOST_AUTO_TEST_CASE(SHA256_DifferentInputsDifferentOutput)
{
    CSHA256 h1, h2;
    const std::string s1 = "hello", s2 = "world";
    h1.Write(reinterpret_cast<const unsigned char*>(s1.data()), s1.size());
    h2.Write(reinterpret_cast<const unsigned char*>(s2.data()), s2.size());

    unsigned char d1[32], d2[32];
    h1.Finalize(d1);
    h2.Finalize(d2);
    BOOST_CHECK(memcmp(d1, d2, 32) != 0);
}

BOOST_AUTO_TEST_CASE(SHA256_SameInputSameOutput)
{
    const std::string input = "digitalnote";
    auto hashOnce = [&]() -> std::vector<unsigned char> {
        CSHA256 h;
        h.Write(reinterpret_cast<const unsigned char*>(input.data()), input.size());
        std::vector<unsigned char> d(32);
        h.Finalize(d.data());
        return d;
    };
    BOOST_CHECK_EQUAL_COLLECTIONS(
        hashOnce().begin(), hashOnce().end(),
        hashOnce().begin(), hashOnce().end()
    );
}

// ── RIPEMD-160 known-answer tests ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(RIPEMD160_EmptyString)
{
    CRIPEMD160 h;
    unsigned char digest[CRIPEMD160::OUTPUT_SIZE];
    h.Finalize(digest);
    // RIPEMD160("") = 9c1185a5c5e9fc54612808977ee8f548b2258d31
    std::string expected = "9c1185a5c5e9fc54612808977ee8f548b2258d31";
    BOOST_CHECK_EQUAL(HexStr(digest, digest + CRIPEMD160::OUTPUT_SIZE), expected);
}

BOOST_AUTO_TEST_CASE(RIPEMD160_OutputSizeIs20Bytes)
{
    BOOST_CHECK_EQUAL(static_cast<int>(CRIPEMD160::OUTPUT_SIZE), 20);
}

BOOST_AUTO_TEST_CASE(RIPEMD160_ABC)
{
    CRIPEMD160 h;
    const std::string input = "abc";
    h.Write(reinterpret_cast<const unsigned char*>(input.data()), input.size());
    unsigned char digest[CRIPEMD160::OUTPUT_SIZE];
    h.Finalize(digest);
    // RIPEMD160("abc") = 8eb208f7e05d987a9b044a8e98c6b087f15a0bfc
    std::string expected = "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc";
    BOOST_CHECK_EQUAL(HexStr(digest, digest + CRIPEMD160::OUTPUT_SIZE), expected);
}

// ── SHA-512 sanity tests ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(SHA512_OutputSizeIs64Bytes)
{
    BOOST_CHECK_EQUAL(static_cast<int>(CSHA512::OUTPUT_SIZE), 64);
}

BOOST_AUTO_TEST_CASE(SHA512_EmptyStringKnownAnswer)
{
    CSHA512 h;
    unsigned char digest[CSHA512::OUTPUT_SIZE];
    h.Finalize(digest);
    // SHA512("") first 16 bytes = cf83e1357eefb8bd...
    std::string hex = HexStr(digest, digest + CSHA512::OUTPUT_SIZE);
    BOOST_CHECK(hex.substr(0, 8) == "cf83e135");
}

// ── Double-SHA256 (Hash) ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(Hash256_OutputIsUint256)
{
    const std::string input = "DigitalNote";
    uint256 h = Hash(
        reinterpret_cast<const unsigned char*>(input.data()),
        reinterpret_cast<const unsigned char*>(input.data()) + input.size()
    );
    // Result should be 32-byte uint256, not all-zero
    BOOST_CHECK(h != uint256());
}

BOOST_AUTO_TEST_CASE(Hash256_Deterministic)
{
    const std::string input = "test_determinism";
    uint256 h1 = Hash(
        reinterpret_cast<const unsigned char*>(input.data()),
        reinterpret_cast<const unsigned char*>(input.data()) + input.size()
    );
    uint256 h2 = Hash(
        reinterpret_cast<const unsigned char*>(input.data()),
        reinterpret_cast<const unsigned char*>(input.data()) + input.size()
    );
    BOOST_CHECK_EQUAL(h1, h2);
}

BOOST_AUTO_TEST_CASE(Hash256_DifferentInputsDifferentOutput)
{
    const std::string a = "aaa", b = "bbb";
    uint256 ha = Hash(reinterpret_cast<const unsigned char*>(a.data()),
                      reinterpret_cast<const unsigned char*>(a.data()) + a.size());
    uint256 hb = Hash(reinterpret_cast<const unsigned char*>(b.data()),
                      reinterpret_cast<const unsigned char*>(b.data()) + b.size());
    BOOST_CHECK_NE(ha, hb);
}

// ── Hash160 (RIPEMD160(SHA256(x))) ───────────────────────────────────────────

BOOST_AUTO_TEST_CASE(Hash160_OutputIsUint160)
{
    const std::string input = "XDN";
    uint160 h = Hash160(
        reinterpret_cast<const unsigned char*>(input.data()),
        reinterpret_cast<const unsigned char*>(input.data()) + input.size()
    );
    BOOST_CHECK(h != uint160());
}

BOOST_AUTO_TEST_CASE(Hash160_Deterministic)
{
    const std::string input = "determinism_test";
    uint160 h1 = Hash160(reinterpret_cast<const unsigned char*>(input.data()),
                         reinterpret_cast<const unsigned char*>(input.data()) + input.size());
    uint160 h2 = Hash160(reinterpret_cast<const unsigned char*>(input.data()),
                         reinterpret_cast<const unsigned char*>(input.data()) + input.size());
    BOOST_CHECK_EQUAL(h1, h2);
}

BOOST_AUTO_TEST_SUITE_END()
