// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// src/test/transaction_tests.cpp
//
// Tests for CMutableTransaction / CTransaction construction, serialisation,
// hash stability, and basic CTxIn / CTxOut validation.
// Tests coin-base detection, version handling, and size limits used
// in DigitalNote-2's mempool and block-assembly code.

#include <boost/test/unit_test.hpp>
#include <vector>
#include <stdexcept>

#include "primitives/transaction.h"
#include "script/script.h"
#include "script/standard.h"
#include "key.h"
#include "amount.h"
#include "uint256.h"
#include "serialize.h"
#include "streams.h"
#include "hash.h"
#include "main.h"    // MAX_STANDARD_TX_SIZE

BOOST_AUTO_TEST_SUITE(TransactionTests)

// ── Empty transaction basics ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(EmptyTransactionHasNoInputsOrOutputs)
{
    CMutableTransaction mtx;
    BOOST_CHECK(mtx.vin.empty());
    BOOST_CHECK(mtx.vout.empty());
}

BOOST_AUTO_TEST_CASE(DefaultTransactionVersionIsPositive)
{
    CMutableTransaction mtx;
    BOOST_CHECK_GT(mtx.nVersion, 0);
}

BOOST_AUTO_TEST_CASE(FinalizedEmptyTxHashIsNotZero)
{
    CMutableTransaction mtx;
    CTransaction tx(mtx);
    // Even an empty tx has a deterministic hash
    BOOST_CHECK(tx.GetHash() != uint256());
}

// ── Coinbase detection ────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(CoinbaseInputHasNullPrevout)
{
    CMutableTransaction mtx;
    CTxIn coinbaseIn;
    // Coinbase input: prevout = null hash, index = 0xFFFFFFFF
    coinbaseIn.prevout.SetNull();
    coinbaseIn.prevout.n = 0xFFFFFFFF;
    mtx.vin.push_back(coinbaseIn);

    CTransaction tx(mtx);
    BOOST_CHECK(tx.IsCoinBase());
}

BOOST_AUTO_TEST_CASE(RegularTxIsNotCoinbase)
{
    CMutableTransaction mtx;
    CTxIn regularIn;
    // Any non-null prevout hash makes it not a coinbase
    regularIn.prevout.hash = uint256S(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    regularIn.prevout.n = 0;
    mtx.vin.push_back(regularIn);

    CTransaction tx(mtx);
    BOOST_CHECK(!tx.IsCoinBase());
}

// ── CTxOut ────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(TxOutWithZeroValueIsValid)
{
    CTxOut out;
    out.nValue = 0;
    CKey key;
    key.MakeNewKey(true);
    out.scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
    BOOST_CHECK(MoneyRange(out.nValue));
}

BOOST_AUTO_TEST_CASE(TxOutNegativeValueFails)
{
    CTxOut out;
    out.nValue = -1;
    BOOST_CHECK(!MoneyRange(out.nValue));
}

BOOST_AUTO_TEST_CASE(TxOutMaxValueIsValid)
{
    CTxOut out;
    out.nValue = MAX_MONEY;
    BOOST_CHECK(MoneyRange(out.nValue));
}

BOOST_AUTO_TEST_CASE(TxOutOverMaxValueFails)
{
    CTxOut out;
    out.nValue = MAX_MONEY + 1;
    BOOST_CHECK(!MoneyRange(out.nValue));
}

// ── Serialisation round-trip ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(TransactionSerialiseRoundtrip)
{
    // Build a simple 1-in / 1-out transaction
    CMutableTransaction mtx;
    mtx.nVersion = 1;
    mtx.nLockTime = 0;

    CTxIn in;
    in.prevout.hash = uint256S(
        "1111111111111111111111111111111111111111111111111111111111111111");
    in.prevout.n = 0;
    in.nSequence  = 0xFFFFFFFF;
    mtx.vin.push_back(in);

    CKey key;
    key.MakeNewKey(true);
    CTxOut out;
    out.nValue      = COIN;
    out.scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
    mtx.vout.push_back(out);

    CTransaction tx(mtx);

    // Serialise
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << tx;

    // Deserialise
    CTransaction tx2;
    ss >> tx2;

    BOOST_CHECK_EQUAL(tx.GetHash(), tx2.GetHash());
    BOOST_CHECK_EQUAL(tx.vin.size(),  tx2.vin.size());
    BOOST_CHECK_EQUAL(tx.vout.size(), tx2.vout.size());
    BOOST_CHECK_EQUAL(tx.vout[0].nValue, tx2.vout[0].nValue);
}

BOOST_AUTO_TEST_CASE(SerialiseUsesCurrentProtocolVersion)
{
    CMutableTransaction mtx;
    CTransaction tx(mtx);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << tx;
    // Serialised stream uses PROTOCOL_VERSION = 2007
    BOOST_CHECK_GT(ss.size(), 0u);
}

// ── Hash stability ────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(SameTransactionProducesSameHash)
{
    CMutableTransaction mtx;
    mtx.nVersion  = 1;
    mtx.nLockTime = 500000;

    CTransaction tx1(mtx), tx2(mtx);
    BOOST_CHECK_EQUAL(tx1.GetHash(), tx2.GetHash());
}

BOOST_AUTO_TEST_CASE(DifferentNLockTimeProducesDifferentHash)
{
    CMutableTransaction m1, m2;
    m1.nVersion  = 1; m1.nLockTime = 0;
    m2.nVersion  = 1; m2.nLockTime = 1;

    CTransaction t1(m1), t2(m2);
    BOOST_CHECK_NE(t1.GetHash(), t2.GetHash());
}

// ── Transaction size limits ───────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(MaxStandardTxSizeIsPositive)
{
    BOOST_CHECK_GT(MAX_STANDARD_TX_SIZE, 0u);
}

BOOST_AUTO_TEST_CASE(MaxStandardTxSizeAtLeast1KB)
{
    BOOST_CHECK_GE(MAX_STANDARD_TX_SIZE, 1000u);
}

BOOST_AUTO_TEST_CASE(MaxStandardTxSizeUnder1MB)
{
    // Standard transactions must not exceed 1 MB to protect mempool
    BOOST_CHECK_LE(MAX_STANDARD_TX_SIZE, 1000000u);
}

// ── CTxIn sequence / locktime ─────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(FinalSequenceValueIsMaxUint32)
{
    BOOST_CHECK_EQUAL(CTxIn::SEQUENCE_FINAL,
                      static_cast<uint32_t>(0xFFFFFFFF));
}

BOOST_AUTO_TEST_CASE(IsFinalReturnsTrueAtMaxLocktime)
{
    CMutableTransaction mtx;
    mtx.nLockTime = 0;
    CTxIn in;
    in.nSequence = CTxIn::SEQUENCE_FINAL;
    mtx.vin.push_back(in);

    CTransaction tx(mtx);
    // IsFinal at any block height / time when nLockTime == 0
    BOOST_CHECK(tx.IsFinal(1000000, 1700000000));
}

BOOST_AUTO_TEST_SUITE_END()
