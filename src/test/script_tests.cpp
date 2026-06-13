// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// src/test/script_tests.cpp
//
// Tests for CScript construction, standard script recognition, and
// script execution primitives used in DigitalNote-2 transaction validation.

#include <boost/test/unit_test.hpp>
#include <vector>
#include <string>

#include "script/script.h"
#include "script/standard.h"
#include "script/interpreter.h"
#include "key.h"
#include "keystore.h"
#include "uint256.h"
#include "hash.h"

BOOST_AUTO_TEST_SUITE(ScriptTests)

// ── CScript construction ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(EmptyScriptIsEmpty)
{
    CScript s;
    BOOST_CHECK(s.empty());
    BOOST_CHECK_EQUAL(s.size(), 0u);
}

BOOST_AUTO_TEST_CASE(PushDataOpcodeRoundtrip)
{
    std::vector<unsigned char> data = {0xDE, 0xAD, 0xBE, 0xEF};
    CScript s;
    s << data;
    BOOST_CHECK(!s.empty());
    // Script should contain a push opcode followed by the data
    BOOST_CHECK_GT(s.size(), data.size());
}

BOOST_AUTO_TEST_CASE(OpcodeOP_RETURNRoundtrip)
{
    CScript s;
    s << OP_RETURN;
    BOOST_CHECK_EQUAL(s.size(), 1u);
    BOOST_CHECK_EQUAL(s[0], static_cast<uint8_t>(OP_RETURN));
}

BOOST_AUTO_TEST_CASE(P2PKHScriptSizeIs25Bytes)
{
    CKey key;
    key.MakeNewKey(true);
    CKeyID keyid = key.GetPubKey().GetID();

    CScript s = GetScriptForDestination(keyid);
    // P2PKH: OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
    // = 1 + 1 + 1 + 20 + 1 + 1 = 25 bytes
    BOOST_CHECK_EQUAL(s.size(), 25u);
}

BOOST_AUTO_TEST_CASE(P2PKHScriptStartsWithOpDup)
{
    CKey key;
    key.MakeNewKey(true);
    CKeyID keyid = key.GetPubKey().GetID();
    CScript s = GetScriptForDestination(keyid);
    BOOST_CHECK_EQUAL(s[0], static_cast<uint8_t>(OP_DUP));
}

BOOST_AUTO_TEST_CASE(P2PKHScriptEndsWithOpChecksig)
{
    CKey key;
    key.MakeNewKey(true);
    CKeyID keyid = key.GetPubKey().GetID();
    CScript s = GetScriptForDestination(keyid);
    BOOST_CHECK_EQUAL(s.back(), static_cast<uint8_t>(OP_CHECKSIG));
}

// ── Standard script recognition ───────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(P2PKHIsRecognisedAsStandard)
{
    CKey key;
    key.MakeNewKey(true);
    CKeyID keyid = key.GetPubKey().GetID();
    CScript s = GetScriptForDestination(keyid);

    txnouttype type;
    std::vector<CTxDestination> dests;
    int nRequired;
    BOOST_CHECK(ExtractDestinations(s, type, dests, nRequired));
    BOOST_CHECK_EQUAL(type, TX_PUBKEYHASH);
    BOOST_CHECK_EQUAL(static_cast<int>(dests.size()), 1);
}

BOOST_AUTO_TEST_CASE(OP_RETURNScriptIsNullData)
{
    CScript s;
    std::vector<unsigned char> data = {0x01, 0x02, 0x03};
    s << OP_RETURN << data;

    txnouttype type;
    std::vector<CTxDestination> dests;
    int nRequired;
    ExtractDestinations(s, type, dests, nRequired);
    BOOST_CHECK_EQUAL(type, TX_NULL_DATA);
}

BOOST_AUTO_TEST_CASE(RandomBytesAreNonstandardScript)
{
    CScript s;
    // Garbage data that doesn't match any standard template
    for (int i = 0; i < 10; i++)
        s << static_cast<unsigned char>(0xFF);

    txnouttype type;
    std::vector<CTxDestination> dests;
    int nRequired;
    ExtractDestinations(s, type, dests, nRequired);
    BOOST_CHECK_EQUAL(type, TX_NONSTANDARD);
}

// ── Multisig script ───────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(P2PKH_2of3MultisigIsRecognised)
{
    CKey k1, k2, k3;
    k1.MakeNewKey(true); k2.MakeNewKey(true); k3.MakeNewKey(true);

    std::vector<CPubKey> keys = {k1.GetPubKey(), k2.GetPubKey(), k3.GetPubKey()};
    CScript s = GetScriptForMultisig(2, keys);

    txnouttype type;
    std::vector<CTxDestination> dests;
    int nRequired;
    BOOST_CHECK(ExtractDestinations(s, type, dests, nRequired));
    BOOST_CHECK_EQUAL(type, TX_MULTISIG);
    BOOST_CHECK_EQUAL(nRequired, 2);
    BOOST_CHECK_EQUAL(static_cast<int>(dests.size()), 3);
}

// ── Script IsUnspendable ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(OP_RETURNScriptIsUnspendable)
{
    CScript s;
    s << OP_RETURN;
    BOOST_CHECK(s.IsUnspendable());
}

BOOST_AUTO_TEST_CASE(P2PKHScriptIsSpendable)
{
    CKey key;
    key.MakeNewKey(true);
    CScript s = GetScriptForDestination(key.GetPubKey().GetID());
    BOOST_CHECK(!s.IsUnspendable());
}

// ── GetDestination round-trip ─────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(GetDestinationRoundtrip)
{
    CKey key;
    key.MakeNewKey(true);
    CKeyID keyid = key.GetPubKey().GetID();
    CScript s    = GetScriptForDestination(keyid);

    CTxDestination dest;
    BOOST_CHECK(ExtractDestination(s, dest));

    CKeyID recovered = boost::get<CKeyID>(dest);
    BOOST_CHECK_EQUAL(recovered, keyid);
}

BOOST_AUTO_TEST_SUITE_END()
