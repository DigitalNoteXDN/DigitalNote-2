// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// src/test/util_tests.cpp
//
// Tests for util.h / utilstrencodings.h helpers: hex encoding, string
// manipulation, argument parsing, time formatting, and sanitisation
// functions used throughout the DigitalNote-2 codebase.

#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>
#include <cstdint>

#include "util.h"
#include "utilstrencodings.h"

BOOST_AUTO_TEST_SUITE(UtilStringTests)

// ── HexStr ────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(HexStr_EmptyInput)
{
    std::vector<unsigned char> empty;
    BOOST_CHECK_EQUAL(HexStr(empty.begin(), empty.end()), "");
}

BOOST_AUTO_TEST_CASE(HexStr_SingleByte)
{
    std::vector<unsigned char> v = {0xAB};
    BOOST_CHECK_EQUAL(HexStr(v.begin(), v.end()), "ab");
}

BOOST_AUTO_TEST_CASE(HexStr_KnownValue)
{
    std::vector<unsigned char> v = {0xDE, 0xAD, 0xBE, 0xEF};
    BOOST_CHECK_EQUAL(HexStr(v.begin(), v.end()), "deadbeef");
}

BOOST_AUTO_TEST_CASE(HexStr_ZeroByte)
{
    std::vector<unsigned char> v = {0x00};
    BOOST_CHECK_EQUAL(HexStr(v.begin(), v.end()), "00");
}

BOOST_AUTO_TEST_CASE(HexStr_AllFF)
{
    std::vector<unsigned char> v = {0xFF, 0xFF, 0xFF};
    BOOST_CHECK_EQUAL(HexStr(v.begin(), v.end()), "ffffff");
}

// ── ParseHex ──────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(ParseHex_EmptyString)
{
    auto v = ParseHex("");
    BOOST_CHECK(v.empty());
}

BOOST_AUTO_TEST_CASE(ParseHex_KnownValue)
{
    auto v = ParseHex("deadbeef");
    BOOST_REQUIRE_EQUAL(v.size(), 4u);
    BOOST_CHECK_EQUAL(v[0], 0xDE);
    BOOST_CHECK_EQUAL(v[1], 0xAD);
    BOOST_CHECK_EQUAL(v[2], 0xBE);
    BOOST_CHECK_EQUAL(v[3], 0xEF);
}

BOOST_AUTO_TEST_CASE(ParseHex_UpperCase)
{
    auto lower = ParseHex("deadbeef");
    auto upper = ParseHex("DEADBEEF");
    BOOST_CHECK_EQUAL_COLLECTIONS(lower.begin(), lower.end(),
                                  upper.begin(), upper.end());
}

BOOST_AUTO_TEST_CASE(ParseHex_RoundtripWithHexStr)
{
    const std::string hex = "0102030405060708090a0b0c0d0e0f";
    auto bytes = ParseHex(hex);
    BOOST_CHECK_EQUAL(HexStr(bytes.begin(), bytes.end()), hex);
}

// ── IsHex ─────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(IsHex_ValidLower)
{
    BOOST_CHECK(IsHex("deadbeef"));
}

BOOST_AUTO_TEST_CASE(IsHex_ValidUpper)
{
    BOOST_CHECK(IsHex("DEADBEEF"));
}

BOOST_AUTO_TEST_CASE(IsHex_ValidMixed)
{
    BOOST_CHECK(IsHex("DeAdBeEf"));
}

BOOST_AUTO_TEST_CASE(IsHex_EmptyIsFalse)
{
    BOOST_CHECK(!IsHex(""));
}

BOOST_AUTO_TEST_CASE(IsHex_OddLengthFalse)
{
    BOOST_CHECK(!IsHex("abc"));
}

BOOST_AUTO_TEST_CASE(IsHex_InvalidCharFalse)
{
    BOOST_CHECK(!IsHex("xyz123"));
}

// ── DecodeBase64 / EncodeBase64 ───────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(Base64_EncodeEmpty)
{
    BOOST_CHECK_EQUAL(EncodeBase64(""), "");
}

BOOST_AUTO_TEST_CASE(Base64_KnownVector)
{
    // "Man" → "TWFu"
    BOOST_CHECK_EQUAL(EncodeBase64("Man"), "TWFu");
}

BOOST_AUTO_TEST_CASE(Base64_RoundtripString)
{
    const std::string original = "DigitalNote XDN v2.0.0.7";
    std::string encoded = EncodeBase64(original);
    bool invalid = false;
    std::string decoded = DecodeBase64(encoded, &invalid);
    BOOST_CHECK(!invalid);
    BOOST_CHECK_EQUAL(decoded, original);
}

BOOST_AUTO_TEST_CASE(Base64_RoundtripBinary)
{
    std::vector<unsigned char> data = {0x00, 0xFF, 0x80, 0x7F, 0x01};
    std::string encoded = EncodeBase64(data.data(), data.size());
    bool invalid = false;
    std::string decoded = DecodeBase64(encoded, &invalid);
    BOOST_CHECK(!invalid);
    BOOST_CHECK_EQUAL(decoded.size(), data.size());
    for (size_t i = 0; i < data.size(); ++i)
        BOOST_CHECK_EQUAL(static_cast<unsigned char>(decoded[i]), data[i]);
}

// ── SanitizeString ────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(SanitizeString_NoopOnSafe)
{
    const std::string safe = "DigitalNote_XDN-2.0.0.7";
    BOOST_CHECK_EQUAL(SanitizeString(safe), safe);
}

BOOST_AUTO_TEST_CASE(SanitizeString_RemovesControlChars)
{
    std::string withCtrl = "hello\x01world";
    std::string sanitized = SanitizeString(withCtrl);
    // Control characters should be stripped or replaced
    BOOST_CHECK(sanitized.find('\x01') == std::string::npos);
}

// ── atoi64 / FormatMoney ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(Atoi64_PositiveNumber)
{
    BOOST_CHECK_EQUAL(atoi64("12345"), 12345LL);
}

BOOST_AUTO_TEST_CASE(Atoi64_NegativeNumber)
{
    BOOST_CHECK_EQUAL(atoi64("-42"), -42LL);
}

BOOST_AUTO_TEST_CASE(Atoi64_Zero)
{
    BOOST_CHECK_EQUAL(atoi64("0"), 0LL);
}

BOOST_AUTO_TEST_CASE(FormatMoney_OneXDN)
{
    // 1 XDN = 100,000,000 satoshis → "1.00000000"
    std::string s = FormatMoney(COIN);
    BOOST_CHECK_EQUAL(s, "1.00000000");
}

BOOST_AUTO_TEST_CASE(FormatMoney_OneCent)
{
    // 1 CENT = 1,000,000 satoshis → "0.01000000"
    std::string s = FormatMoney(CENT);
    BOOST_CHECK_EQUAL(s, "0.01000000");
}

BOOST_AUTO_TEST_CASE(FormatMoney_Zero)
{
    BOOST_CHECK_EQUAL(FormatMoney(0), "0.00000000");
}

BOOST_AUTO_TEST_CASE(ParseMoney_RoundtripOneCoin)
{
    CAmount val = 0;
    BOOST_CHECK(ParseMoney("1.00000000", val));
    BOOST_CHECK_EQUAL(val, COIN);
}

BOOST_AUTO_TEST_CASE(ParseMoney_RejectsNegative)
{
    CAmount val = 0;
    BOOST_CHECK(!ParseMoney("-1.0", val));
}

// ── itostr / strprintf ────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(Strprintf_BasicFormat)
{
    std::string s = strprintf("%s %d", "XDN", 2007);
    BOOST_CHECK_EQUAL(s, "XDN 2007");
}

BOOST_AUTO_TEST_CASE(Strprintf_EmptyFormat)
{
    BOOST_CHECK_EQUAL(strprintf(""), "");
}

BOOST_AUTO_TEST_SUITE_END()
