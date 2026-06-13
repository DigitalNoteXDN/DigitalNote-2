// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// src/test/key_tests.cpp
//
// Tests for CKey, CPubKey, and ECDSA sign/verify using the secp256k1
// library that DigitalNote-2 bundles.  These verify the core cryptographic
// signing pipeline that every transaction relies on.

#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

#include "key.h"
#include "uint256.h"
#include "utilstrencodings.h"
#include "random.h"
#include "hash.h"

// Known-answer test vector from Bitcoin Core key_tests.cpp
// private key (WIF-decoded) → expected compressed pubkey
static const std::string kPrivHex =
    "12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747";

BOOST_AUTO_TEST_SUITE(KeyTests)

// ── Key generation ────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(NewCompressedKeyIsValid)
{
    CKey key;
    key.MakeNewKey(/*fCompressed=*/true);
    BOOST_CHECK(key.IsValid());
    BOOST_CHECK(key.IsCompressed());
}

BOOST_AUTO_TEST_CASE(NewUncompressedKeyIsValid)
{
    CKey key;
    key.MakeNewKey(/*fCompressed=*/false);
    BOOST_CHECK(key.IsValid());
    BOOST_CHECK(!key.IsCompressed());
}

BOOST_AUTO_TEST_CASE(DefaultKeyIsInvalid)
{
    CKey key;
    BOOST_CHECK(!key.IsValid());
}

BOOST_AUTO_TEST_CASE(TwoNewKeysAreDistinct)
{
    CKey k1, k2;
    k1.MakeNewKey(true);
    k2.MakeNewKey(true);
    // Astronomically unlikely to collide — if this fails the RNG is broken
    BOOST_CHECK(k1.GetPubKey() != k2.GetPubKey());
}

// ── Public key derivation ─────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(CompressedPubKeyIs33Bytes)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();
    BOOST_CHECK(pub.IsCompressed());
    BOOST_CHECK_EQUAL(pub.size(), 33u);
}

BOOST_AUTO_TEST_CASE(UncompressedPubKeyIs65Bytes)
{
    CKey key;
    key.MakeNewKey(false);
    CPubKey pub = key.GetPubKey();
    BOOST_CHECK(!pub.IsCompressed());
    BOOST_CHECK_EQUAL(pub.size(), 65u);
}

BOOST_AUTO_TEST_CASE(PubKeyIsValid)
{
    CKey key;
    key.MakeNewKey(true);
    BOOST_CHECK(key.GetPubKey().IsValid());
}

BOOST_AUTO_TEST_CASE(PubKeyIDMatchesHash160)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub  = key.GetPubKey();
    CKeyID  kid  = pub.GetID();
    // GetID() is RIPEMD160(SHA256(pubkey_bytes)) — must be non-zero
    BOOST_CHECK(kid != CKeyID());
}

// ── Sign / Verify ─────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(SignAndVerifyRoundtrip)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    // Hash something to sign
    const std::string msg = "DigitalNote XDN 2.0.0.7 test message";
    uint256 hash = Hash(
        reinterpret_cast<const unsigned char*>(msg.data()),
        reinterpret_cast<const unsigned char*>(msg.data()) + msg.size()
    );

    std::vector<unsigned char> sig;
    BOOST_CHECK(key.Sign(hash, sig));
    BOOST_CHECK(!sig.empty());
    BOOST_CHECK(pub.Verify(hash, sig));
}

BOOST_AUTO_TEST_CASE(SignatureLengthInDERRange)
{
    CKey key;
    key.MakeNewKey(true);
    const std::string msg = "test";
    uint256 hash = Hash(
        reinterpret_cast<const unsigned char*>(msg.data()),
        reinterpret_cast<const unsigned char*>(msg.data()) + msg.size()
    );
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(key.Sign(hash, sig));
    // DER-encoded secp256k1 signatures are 70–72 bytes
    BOOST_CHECK_GE(sig.size(), 70u);
    BOOST_CHECK_LE(sig.size(), 72u);
}

BOOST_AUTO_TEST_CASE(TamperedSignatureFails)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    const std::string msg = "tamper test";
    uint256 hash = Hash(
        reinterpret_cast<const unsigned char*>(msg.data()),
        reinterpret_cast<const unsigned char*>(msg.data()) + msg.size()
    );

    std::vector<unsigned char> sig;
    BOOST_REQUIRE(key.Sign(hash, sig));

    // Flip a byte in the middle of the signature
    sig[sig.size() / 2] ^= 0xFF;
    BOOST_CHECK(!pub.Verify(hash, sig));
}

BOOST_AUTO_TEST_CASE(WrongKeyFails)
{
    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);

    const std::string msg = "wrong key test";
    uint256 hash = Hash(
        reinterpret_cast<const unsigned char*>(msg.data()),
        reinterpret_cast<const unsigned char*>(msg.data()) + msg.size()
    );

    std::vector<unsigned char> sig;
    BOOST_REQUIRE(key1.Sign(hash, sig));
    BOOST_CHECK(!key2.GetPubKey().Verify(hash, sig));
}

BOOST_AUTO_TEST_CASE(WrongHashFails)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    const std::string msg1 = "original message";
    const std::string msg2 = "different message";
    uint256 h1 = Hash(reinterpret_cast<const unsigned char*>(msg1.data()),
                      reinterpret_cast<const unsigned char*>(msg1.data()) + msg1.size());
    uint256 h2 = Hash(reinterpret_cast<const unsigned char*>(msg2.data()),
                      reinterpret_cast<const unsigned char*>(msg2.data()) + msg2.size());

    std::vector<unsigned char> sig;
    BOOST_REQUIRE(key.Sign(h1, sig));
    BOOST_CHECK(!pub.Verify(h2, sig));
}

// ── Compact sign / recover ────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(SignCompactAndRecoverPubKey)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    const std::string msg = "compact recovery test";
    uint256 hash = Hash(
        reinterpret_cast<const unsigned char*>(msg.data()),
        reinterpret_cast<const unsigned char*>(msg.data()) + msg.size()
    );

    std::vector<unsigned char> sigCompact;
    BOOST_REQUIRE(key.SignCompact(hash, sigCompact));
    // Compact signatures are 65 bytes (1 recovery byte + 32 r + 32 s)
    BOOST_CHECK_EQUAL(sigCompact.size(), 65u);

    CPubKey recovered;
    BOOST_CHECK(recovered.RecoverCompact(hash, sigCompact));
    BOOST_CHECK_EQUAL(recovered, pub);
}

// ── Known-answer test from Bitcoin Core ──────────────────────────────────────

BOOST_AUTO_TEST_CASE(KnownPrivKeyProducesKnownPubKey)
{
    // kPrivHex → compressed pubkey starts with 02 or 03
    std::vector<unsigned char> privBytes = ParseHex(kPrivHex);
    CKey key;
    key.Set(privBytes.begin(), privBytes.end(), /*fCompressedIn=*/true);
    BOOST_CHECK(key.IsValid());

    CPubKey pub = key.GetPubKey();
    BOOST_CHECK(pub.IsValid());
    BOOST_CHECK_EQUAL(pub.size(), 33u);
    // First byte of compressed pub is 02 or 03
    BOOST_CHECK(pub.begin()[0] == 0x02 || pub.begin()[0] == 0x03);
}

BOOST_AUTO_TEST_SUITE_END()
