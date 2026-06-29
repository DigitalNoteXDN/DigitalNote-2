// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// src/test/base58_tests.cpp
//
// Tests for DigitalNote base58 address encoding/decoding.
// Uses real chain params (mainnet prefix 0x19 = "X" addresses).
// Compiled into test_digitalnote.

#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

#include "base58.h"
#include "chainparams.h"
#include "key.h"
#include "uint256.h"
#include "util.h"

BOOST_AUTO_TEST_SUITE(Base58Tests)

// ── Encode / decode round-trip ────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(EncodeDecodeRoundtrip)
{
    const std::vector<unsigned char> data = {0x00, 0x01, 0x02, 0xAB, 0xCD, 0xEF};
    std::string encoded = EncodeBase58(data);
    BOOST_CHECK(!encoded.empty());

    std::vector<unsigned char> decoded;
    BOOST_CHECK(DecodeBase58(encoded, decoded));
    BOOST_CHECK_EQUAL_COLLECTIONS(decoded.begin(), decoded.end(),
                                  data.begin(),    data.end());
}

BOOST_AUTO_TEST_CASE(EmptyBytesEncodesEmpty)
{
    std::vector<unsigned char> empty;
    std::string encoded = EncodeBase58(empty);
    // Some implementations encode empty as "" or "1" — test consistency
    std::vector<unsigned char> decoded;
    BOOST_CHECK(DecodeBase58(encoded, decoded));
    BOOST_CHECK(decoded.empty());
}

BOOST_AUTO_TEST_CASE(LeadingZeroesPreserved)
{
    std::vector<unsigned char> data = {0x00, 0x00, 0x01};
    std::string encoded = EncodeBase58(data);
    std::vector<unsigned char> decoded;
    BOOST_CHECK(DecodeBase58(encoded, decoded));
    BOOST_CHECK_EQUAL_COLLECTIONS(decoded.begin(), decoded.end(),
                                  data.begin(),    data.end());
}

BOOST_AUTO_TEST_CASE(InvalidCharacterRejected)
{
    std::vector<unsigned char> decoded;
    // '0', 'O', 'I', 'l' are excluded from base58
    BOOST_CHECK(!DecodeBase58("0InvalidChar", decoded));
}

// ── Checksummed encoding ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(EncodeDecodeCheckRoundtrip)
{
    const std::vector<unsigned char> data = {0xDE, 0xAD, 0xBE, 0xEF};
    std::string encoded = EncodeBase58Check(data);
    BOOST_CHECK(!encoded.empty());

    std::vector<unsigned char> decoded;
    BOOST_CHECK(DecodeBase58Check(encoded, decoded));
    BOOST_CHECK_EQUAL_COLLECTIONS(decoded.begin(), decoded.end(),
                                  data.begin(),    data.end());
}

BOOST_AUTO_TEST_CASE(CorruptedChecksumRejected)
{
    const std::vector<unsigned char> data = {0x01, 0x02, 0x03};
    std::string encoded = EncodeBase58Check(data);
    BOOST_CHECK(!encoded.empty());

    // Flip a character in the middle
    std::string corrupted = encoded;
    corrupted[corrupted.size() / 2] ^= 0x01;

    std::vector<unsigned char> decoded;
    BOOST_CHECK(!DecodeBase58Check(corrupted, decoded));
}

// ── CBitcoinAddress / DigitalNote mainnet prefix ──────────────────────────────
// DigitalNote mainnet P2PKH prefix = 0x19 (decimal 25) → addresses start with "X"
// Mainnet P2SH prefix = 0x0D (decimal 13) → addresses start with "6"

BOOST_AUTO_TEST_CASE(MainnetP2PKHAddressStartsWithX)
{
    SelectParams(CBaseChainParams::MAIN);

    // Generate a throw-away key
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    CKeyID keyid   = pubkey.GetID();

    CBitcoinAddress addr(keyid);
    BOOST_CHECK(addr.IsValid());

    std::string addrStr = addr.ToString();
    BOOST_CHECK(!addrStr.empty());
    // DigitalNote mainnet addresses start with 'X'
    BOOST_CHECK_EQUAL(addrStr[0], 'X');
}

BOOST_AUTO_TEST_CASE(InvalidAddressStringRejected)
{
    SelectParams(CBaseChainParams::MAIN);
    CBitcoinAddress addr("not_a_valid_address");
    BOOST_CHECK(!addr.IsValid());
}

BOOST_AUTO_TEST_CASE(EmptyAddressStringRejected)
{
    SelectParams(CBaseChainParams::MAIN);
    CBitcoinAddress addr("");
    BOOST_CHECK(!addr.IsValid());
}

BOOST_AUTO_TEST_CASE(MainnetAddressRoundtrip)
{
    SelectParams(CBaseChainParams::MAIN);

    CKey key;
    key.MakeNewKey(true);
    CPubKey pub    = key.GetPubKey();
    CKeyID  keyid  = pub.GetID();

    CBitcoinAddress addr1(keyid);
    BOOST_CHECK(addr1.IsValid());

    CBitcoinAddress addr2(addr1.ToString());
    BOOST_CHECK(addr2.IsValid());
    BOOST_CHECK_EQUAL(addr1.ToString(), addr2.ToString());

    CKeyID keyid2;
    BOOST_CHECK(addr2.GetKeyID(keyid2));
    BOOST_CHECK(keyid == keyid2);
}

BOOST_AUTO_TEST_CASE(TestnetAddressHasDifferentPrefix)
{
    // Testnet and mainnet should produce different address strings for same key
    CKey key;
    key.MakeNewKey(true);
    CKeyID keyid = key.GetPubKey().GetID();

    SelectParams(CBaseChainParams::MAIN);
    std::string mainAddr = CBitcoinAddress(keyid).ToString();

    SelectParams(CBaseChainParams::TESTNET);
    std::string testAddr = CBitcoinAddress(keyid).ToString();

    BOOST_CHECK_NE(mainAddr, testAddr);

    // Restore mainnet for other tests
    SelectParams(CBaseChainParams::MAIN);
}

BOOST_AUTO_TEST_SUITE_END()
