// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// src/test/amount_tests.cpp
//
// Tests for CAmount, monetary unit limits, and fee arithmetic.
// Uses real DigitalNote-2 constants from amount.h / main.h.
// Compiled into test_digitalnote alongside existing tests.

#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <limits>

#include "amount.h"        // CAmount, COIN, CENT
#include "util.h"
// main.h for MIN_TX_FEE, MIN_RELAY_TX_FEE, MAX_MONEY — include guard-safe
#include "main.h"

BOOST_AUTO_TEST_SUITE(AmountTests)

// ── Monetary constants ────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(CoinEquals100000000Satoshis)
{
    BOOST_CHECK_EQUAL(COIN, CAmount(100000000));
}

BOOST_AUTO_TEST_CASE(CentEquals1000000Satoshis)
{
    BOOST_CHECK_EQUAL(CENT, CAmount(1000000));
}

BOOST_AUTO_TEST_CASE(MaxMoneyIsPositive)
{
    BOOST_CHECK_GT(MAX_MONEY, CAmount(0));
}

BOOST_AUTO_TEST_CASE(MaxMoneyFitsInInt64)
{
    BOOST_CHECK_LE(MAX_MONEY,
                   static_cast<CAmount>(std::numeric_limits<int64_t>::max()));
}

BOOST_AUTO_TEST_CASE(MoneyRangeAcceptsZero)
{
    BOOST_CHECK(MoneyRange(CAmount(0)));
}

BOOST_AUTO_TEST_CASE(MoneyRangeAcceptsMaxMoney)
{
    BOOST_CHECK(MoneyRange(MAX_MONEY));
}

BOOST_AUTO_TEST_CASE(MoneyRangeRejectsNegative)
{
    BOOST_CHECK(!MoneyRange(CAmount(-1)));
    BOOST_CHECK(!MoneyRange(CAmount(-COIN)));
}

BOOST_AUTO_TEST_CASE(MoneyRangeRejectsOverMax)
{
    BOOST_CHECK(!MoneyRange(MAX_MONEY + 1));
}

// ── Fee constants ─────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(MinTxFeeIsPositive)
{
    BOOST_CHECK_GT(MIN_TX_FEE, CAmount(0));
}

BOOST_AUTO_TEST_CASE(MinRelayTxFeeIsPositive)
{
    BOOST_CHECK_GT(MIN_RELAY_TX_FEE, CAmount(0));
}

BOOST_AUTO_TEST_CASE(MinRelayTxFeeNotGreaterThanMinTxFee)
{
    // Relay fee should be ≤ min transaction fee
    BOOST_CHECK_LE(MIN_RELAY_TX_FEE, MIN_TX_FEE);
}

BOOST_AUTO_TEST_CASE(MinTxFeeUnderMaxMoney)
{
    BOOST_CHECK_LT(MIN_TX_FEE, MAX_MONEY);
}

// ── Arithmetic safety ─────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(CoinMultiplicationDoesNotOverflow)
{
    // 21 million XDN * COIN should not exceed int64 max
    // DigitalNote total supply is well under 21M, but check the arithmetic
    CAmount supply = CAmount(21000000) * COIN;
    BOOST_CHECK_GT(supply, CAmount(0));
    BOOST_CHECK(MoneyRange(supply));
}

BOOST_AUTO_TEST_CASE(SmallAmountsAddCorrectly)
{
    CAmount a = CENT;
    CAmount b = CENT * 2;
    BOOST_CHECK_EQUAL(a + b, CAmount(3) * CENT);
}

BOOST_AUTO_TEST_CASE(AmountSubtractionIsCorrect)
{
    CAmount a = COIN;
    CAmount b = CENT;
    BOOST_CHECK_EQUAL(a - b, CAmount(99) * CENT);
}

BOOST_AUTO_TEST_SUITE_END()
