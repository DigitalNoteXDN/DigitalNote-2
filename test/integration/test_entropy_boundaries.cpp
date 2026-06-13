// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// test/integration/test_entropy_boundaries.cpp
// Tests for edge cases in entropy size handling, invalid inputs,
// and boundary conditions across the BIP39 pipeline.

#define BOOST_TEST_MODULE EntropyBoundaryTests
#include <boost/test/unit_test.hpp>

#include "bip39/entropy.h"
#include "bip39/checksum.h"
#include "bip39/mnemonic.h"
#include "bip39/seed.h"
#include "bip39/bip39_wallet.h"
#include "database.h"

#include <vector>
#include <string>
#include <stdexcept>

// ─── Suite 1: Valid entropy sizes ─────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(ValidEntropySizes)

BOOST_AUTO_TEST_CASE(Entropy128Bit_Produces12Words)
{
    std::vector<uint8_t> entropy(16, 0xAA);
    BIP39::Entropy  ent(entropy);
    BIP39::Checksum cs(ent);
    BIP39::Mnemonic mn(cs, BIP39::WordList::English);
    auto words = mn.toVector();
    BOOST_CHECK_EQUAL(static_cast<int>(words.size()), 12);
}

BOOST_AUTO_TEST_CASE(Entropy160Bit_Produces15Words)
{
    std::vector<uint8_t> entropy(20, 0xBB);
    BIP39::Entropy  ent(entropy);
    BIP39::Checksum cs(ent);
    BIP39::Mnemonic mn(cs, BIP39::WordList::English);
    BOOST_CHECK_EQUAL(static_cast<int>(mn.toVector().size()), 15);
}

BOOST_AUTO_TEST_CASE(Entropy192Bit_Produces18Words)
{
    std::vector<uint8_t> entropy(24, 0xCC);
    BIP39::Entropy  ent(entropy);
    BIP39::Checksum cs(ent);
    BIP39::Mnemonic mn(cs, BIP39::WordList::English);
    BOOST_CHECK_EQUAL(static_cast<int>(mn.toVector().size()), 18);
}

BOOST_AUTO_TEST_CASE(Entropy224Bit_Produces21Words)
{
    std::vector<uint8_t> entropy(28, 0xDD);
    BIP39::Entropy  ent(entropy);
    BIP39::Checksum cs(ent);
    BIP39::Mnemonic mn(cs, BIP39::WordList::English);
    BOOST_CHECK_EQUAL(static_cast<int>(mn.toVector().size()), 21);
}

BOOST_AUTO_TEST_CASE(Entropy256Bit_Produces24Words)
{
    std::vector<uint8_t> entropy(32, 0xEE);
    BIP39::Entropy  ent(entropy);
    BIP39::Checksum cs(ent);
    BIP39::Mnemonic mn(cs, BIP39::WordList::English);
    BOOST_CHECK_EQUAL(static_cast<int>(mn.toVector().size()), 24);
}

BOOST_AUTO_TEST_CASE(AllZeroEntropy_StillValid)
{
    // All-zero is a degenerate but valid entropy — "abandon abandon... about"
    std::vector<uint8_t> entropy(16, 0x00);
    BIP39::Entropy  ent(entropy);
    BIP39::Checksum cs(ent);
    BIP39::Mnemonic mn(cs, BIP39::WordList::English);
    BOOST_CHECK_EQUAL(static_cast<int>(mn.toVector().size()), 12);
    BOOST_CHECK(BIP39::Mnemonic::validate(mn.toString(), BIP39::WordList::English));
}

BOOST_AUTO_TEST_CASE(AllOneEntropy_StillValid)
{
    std::vector<uint8_t> entropy(32, 0xFF);
    BIP39::Entropy  ent(entropy);
    BIP39::Checksum cs(ent);
    BIP39::Mnemonic mn(cs, BIP39::WordList::English);
    BOOST_CHECK_EQUAL(static_cast<int>(mn.toVector().size()), 24);
    BOOST_CHECK(BIP39::Mnemonic::validate(mn.toString(), BIP39::WordList::English));
}

BOOST_AUTO_TEST_CASE(RandomEntropy_ProducesValidMnemonic)
{
    // Use a fixed "random" seed for reproducibility
    std::vector<uint8_t> entropy = {
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00,
        0x13, 0x24, 0x35, 0x46, 0x57, 0x68, 0x79, 0x8A
    };
    BIP39::Entropy  ent(entropy);
    BIP39::Checksum cs(ent);
    BIP39::Mnemonic mn(cs, BIP39::WordList::English);
    BOOST_CHECK(BIP39::Mnemonic::validate(mn.toString(), BIP39::WordList::English));
    BOOST_CHECK_EQUAL(static_cast<int>(mn.toVector().size()), 24);
}

BOOST_AUTO_TEST_SUITE_END()

// ─── Suite 2: Invalid entropy sizes ──────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(InvalidEntropySizes)

BOOST_AUTO_TEST_CASE(EmptyEntropy_ThrowsOrRejects)
{
    std::vector<uint8_t> entropy;
    BOOST_CHECK_THROW(BIP39::Entropy ent(entropy), std::exception);
}

BOOST_AUTO_TEST_CASE(TooShortEntropy_ThrowsOrRejects)
{
    // 15 bytes = 120 bits — not a valid BIP39 size (must be multiple of 32)
    std::vector<uint8_t> entropy(15, 0xAA);
    BOOST_CHECK_THROW(BIP39::Entropy ent(entropy), std::exception);
}

BOOST_AUTO_TEST_CASE(TooLongEntropy_ThrowsOrRejects)
{
    // 33 bytes = 264 bits — exceeds BIP39 maximum of 256 bits
    std::vector<uint8_t> entropy(33, 0xAA);
    BOOST_CHECK_THROW(BIP39::Entropy ent(entropy), std::exception);
}

BOOST_AUTO_TEST_CASE(OddSizeEntropy_ThrowsOrRejects)
{
    // 17 bytes = 136 bits — not a multiple of 32 bits
    std::vector<uint8_t> entropy(17, 0xAA);
    BOOST_CHECK_THROW(BIP39::Entropy ent(entropy), std::exception);
}

BOOST_AUTO_TEST_SUITE_END()

// ─── Suite 3: Mnemonic validation edge cases ──────────────────────────────────

BOOST_AUTO_TEST_SUITE(MnemonicValidationEdgeCases)

BOOST_AUTO_TEST_CASE(MnemonicWith11Words_Invalid)
{
    std::string mn = "abandon abandon abandon abandon abandon abandon "
                     "abandon abandon abandon abandon abandon";
    BOOST_CHECK(!BIP39::Mnemonic::validate(mn, BIP39::WordList::English));
}

BOOST_AUTO_TEST_CASE(MnemonicWith13Words_Invalid)
{
    std::string mn = "abandon abandon abandon abandon abandon abandon "
                     "abandon abandon abandon abandon abandon about zoo";
    BOOST_CHECK(!BIP39::Mnemonic::validate(mn, BIP39::WordList::English));
}

BOOST_AUTO_TEST_CASE(MnemonicWithMixedCase_Behaviour)
{
    // BIP39 spec says words should be lowercase.
    // Test documents whether the library normalises or rejects mixed case.
    std::string mn_lower = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    std::string mn_upper = "Abandon Abandon Abandon Abandon Abandon Abandon "
                           "Abandon Abandon Abandon Abandon Abandon About";

    bool lower_valid = BIP39::Mnemonic::validate(mn_lower, BIP39::WordList::English);
    bool upper_valid = BIP39::Mnemonic::validate(mn_upper, BIP39::WordList::English);

    BOOST_CHECK(lower_valid);
    // Upper case: acceptable if library normalises, not acceptable if strict
    BOOST_TEST_MESSAGE("Upper-case mnemonic accepted by library: " << upper_valid);
}

BOOST_AUTO_TEST_CASE(MnemonicWithTabSeparator_Behaviour)
{
    // Some wallets use tab-separated mnemonics — test library behaviour
    std::string mn = "abandon\tabandon\tabandon\tabandon\tabandon\tabandon\t"
                     "abandon\tabandon\tabandon\tabandon\tabandon\tabout";
    bool result = BIP39::Mnemonic::validate(mn, BIP39::WordList::English);
    BOOST_TEST_MESSAGE("Tab-separated mnemonic accepted: " << result);
    // No hard assertion — documents the library's behaviour
}

BOOST_AUTO_TEST_CASE(MnemonicWithLeadingTrailingSpaces_Behaviour)
{
    std::string mn = "  abandon abandon abandon abandon abandon abandon "
                     "abandon abandon abandon abandon abandon about  ";
    bool result = BIP39::Mnemonic::validate(mn, BIP39::WordList::English);
    BOOST_TEST_MESSAGE("Whitespace-padded mnemonic accepted: " << result);
}

BOOST_AUTO_TEST_SUITE_END()

// ─── Suite 4: BIP39Wallet::entropyBits helper ─────────────────────────────────

BOOST_AUTO_TEST_SUITE(EntropyBitsHelper)

BOOST_AUTO_TEST_CASE(Bits_MultipleOf32)
{
    using WC = BIP39Wallet::WordCount;
    for (auto wc : {WC::Words12, WC::Words15, WC::Words18, WC::Words21, WC::Words24}) {
        int bits = BIP39Wallet::entropyBits(wc);
        BOOST_CHECK_EQUAL(bits % 32, 0);
    }
}

BOOST_AUTO_TEST_CASE(Bits_WithinBIP39Range)
{
    using WC = BIP39Wallet::WordCount;
    for (auto wc : {WC::Words12, WC::Words15, WC::Words18, WC::Words21, WC::Words24}) {
        int bits = BIP39Wallet::entropyBits(wc);
        BOOST_CHECK_GE(bits, 128);
        BOOST_CHECK_LE(bits, 256);
    }
}

BOOST_AUTO_TEST_CASE(Bits_MonotonicallyIncreasingWithWordCount)
{
    using WC = BIP39Wallet::WordCount;
    BOOST_CHECK_LT(BIP39Wallet::entropyBits(WC::Words12), BIP39Wallet::entropyBits(WC::Words15));
    BOOST_CHECK_LT(BIP39Wallet::entropyBits(WC::Words15), BIP39Wallet::entropyBits(WC::Words18));
    BOOST_CHECK_LT(BIP39Wallet::entropyBits(WC::Words18), BIP39Wallet::entropyBits(WC::Words21));
    BOOST_CHECK_LT(BIP39Wallet::entropyBits(WC::Words21), BIP39Wallet::entropyBits(WC::Words24));
}

BOOST_AUTO_TEST_SUITE_END()
