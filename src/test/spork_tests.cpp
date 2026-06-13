// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// src/test/spork_tests.cpp
//
// Tests for the DigitalNote spork system, masternode collateral constants,
// and PoS kernel parameters.  These are the DigitalNote-specific features
// that distinguish the codebase from vanilla Bitcoin Core.

#include <boost/test/unit_test.hpp>

#include "spork.h"
#include "masternode.h"       // MN_COLLATERAL, MASTERNODE_MIN_CONFIRMATIONS
#include "kernel.h"           // STAKE_MIN_AGE, STAKE_MAX_AGE, nStakeMinDepth
#include "main.h"             // nStakeMinAge, COIN_YEAR_REWARD etc.
#include "chainparams.h"
#include "version.h"

BOOST_AUTO_TEST_SUITE(SporkTests)

// ── Spork ID constants ────────────────────────────────────────────────────────
// These IDs must not change between releases — peers cross-check them.

BOOST_AUTO_TEST_CASE(SporkInstantTXHasExpectedID)
{
    // SPORK_2_INSTANTX is typically ID 10002 in Dash-derived coins
    BOOST_CHECK_EQUAL(SPORK_2_INSTANTX, 10002);
}

BOOST_AUTO_TEST_CASE(SporkInstantTXBlockFiltersHasExpectedID)
{
    BOOST_CHECK_EQUAL(SPORK_3_INSTANTX_BLOCK_FILTERING, 10003);
}

BOOST_AUTO_TEST_CASE(SporkMasternodePaymentsHasExpectedID)
{
    BOOST_CHECK_EQUAL(SPORK_9_MASTERNODE_SUPERBLOCKS, 10009);
}

BOOST_AUTO_TEST_CASE(SporkIDsAreUnique)
{
    // IDs must all be distinct — a collision would mean two features share
    // the same broadcast signal
    std::vector<int> ids = {
        SPORK_2_INSTANTX,
        SPORK_3_INSTANTX_BLOCK_FILTERING,
        SPORK_9_MASTERNODE_SUPERBLOCKS,
    };
    for (size_t i = 0; i < ids.size(); ++i)
        for (size_t j = i + 1; j < ids.size(); ++j)
            BOOST_CHECK_NE(ids[i], ids[j]);
}

BOOST_AUTO_TEST_CASE(SporkIDsArePositive)
{
    BOOST_CHECK_GT(SPORK_2_INSTANTX, 0);
    BOOST_CHECK_GT(SPORK_3_INSTANTX_BLOCK_FILTERING, 0);
}

// ── IsSporkActive default state ────────────────────────────────────────────────
// Without a live network, sporks not yet broadcast should return the
// hard-coded default (active or inactive depending on spork).

BOOST_AUTO_TEST_CASE(IsSporkActiveReturnsBool)
{
    // Just verify it compiles and returns without crashing
    bool result = IsSporkActive(SPORK_2_INSTANTX);
    BOOST_CHECK(result == true || result == false); // tautology — checks no crash
}

BOOST_AUTO_TEST_SUITE_END()

// ─────────────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(MasternodeConstantTests)

BOOST_AUTO_TEST_CASE(MasternodeCollateralIs1MillionXDN)
{
    // DigitalNote masternode collateral = 1,000,000 XDN
    SelectParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(MN_COLLATERAL, CAmount(1000000) * COIN);
}

BOOST_AUTO_TEST_CASE(MasternodeMinConfirmationsIsPositive)
{
    BOOST_CHECK_GT(MASTERNODE_MIN_CONFIRMATIONS, 0);
}

BOOST_AUTO_TEST_CASE(MasternodeMinConfirmationsAtLeast15)
{
    // PoS-v3 requires 15 confirmations minimum
    BOOST_CHECK_GE(MASTERNODE_MIN_CONFIRMATIONS, 15);
}

BOOST_AUTO_TEST_CASE(MasternodePortIs18092)
{
    SelectParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(Params().GetDefaultPort(), 18092);
}

BOOST_AUTO_TEST_CASE(RPCPortIs18094)
{
    SelectParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(Params().RPCPort(), 18094);
}

BOOST_AUTO_TEST_SUITE_END()

// ─────────────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(PoSKernelTests)

// DigitalNote README: "Stake Minimum Age: 15 Confirmations (PoS-v3) | 30 Minutes (PoS-v2)"

BOOST_AUTO_TEST_CASE(StakeMinDepthIs15)
{
    // PoS-v3: minimum stake depth is 15 confirmations
    BOOST_CHECK_EQUAL(nStakeMinDepth, 15);
}

BOOST_AUTO_TEST_CASE(StakeMinAgeIsPositive)
{
    BOOST_CHECK_GT(nStakeMinAge, 0);
}

BOOST_AUTO_TEST_CASE(StakeMinAgeAtLeast30Minutes)
{
    // PoS-v2 minimum = 30 minutes = 1800 seconds
    BOOST_CHECK_GE(nStakeMinAge, 1800);
}

BOOST_AUTO_TEST_CASE(StakeMaxAgeGreaterThanMinAge)
{
    BOOST_CHECK_GT(nStakeMaxAge, nStakeMinAge);
}

BOOST_AUTO_TEST_CASE(BlockSpacingIs2Minutes)
{
    // README: "Block Spacing: 2 Minutes"
    // nTargetSpacing is typically in seconds
    SelectParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(Params().GetConsensus().nTargetSpacing, 120); // 2 * 60
}

BOOST_AUTO_TEST_SUITE_END()

// ─────────────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(ChainParamsTests)

BOOST_AUTO_TEST_CASE(MainnetMessageStart4Bytes)
{
    SelectParams(CBaseChainParams::MAIN);
    // pchMessageStart is 4 bytes — must all be non-zero to distinguish from noise
    const CMessageHeader::MessageStartChars& start = Params().MessageStart();
    bool allZero = (start[0] == 0 && start[1] == 0 && start[2] == 0 && start[3] == 0);
    BOOST_CHECK(!allZero);
}

BOOST_AUTO_TEST_CASE(TestnetHasDifferentMessageStart)
{
    SelectParams(CBaseChainParams::MAIN);
    CMessageHeader::MessageStartChars mainStart;
    std::copy(std::begin(Params().MessageStart()),
              std::end(Params().MessageStart()),
              std::begin(mainStart));

    SelectParams(CBaseChainParams::TESTNET);
    CMessageHeader::MessageStartChars testStart;
    std::copy(std::begin(Params().MessageStart()),
              std::end(Params().MessageStart()),
              std::begin(testStart));

    bool same = (mainStart[0] == testStart[0] &&
                 mainStart[1] == testStart[1] &&
                 mainStart[2] == testStart[2] &&
                 mainStart[3] == testStart[3]);
    BOOST_CHECK(!same);

    SelectParams(CBaseChainParams::MAIN);
}

BOOST_AUTO_TEST_CASE(MainnetGenesisBlockNonZero)
{
    SelectParams(CBaseChainParams::MAIN);
    const CBlock& genesis = Params().GenesisBlock();
    BOOST_CHECK_GT(genesis.nTime, 0u);
    BOOST_CHECK(genesis.GetHash() != uint256());
}

BOOST_AUTO_TEST_CASE(SeedNodesExistOnMainnet)
{
    SelectParams(CBaseChainParams::MAIN);
    // The mainnet should have at least one fixed seed or DNS seed
    bool hasDNSSeeds  = !Params().DNSSeeds().empty();
    bool hasFixedSeeds = !Params().FixedSeeds().empty();
    BOOST_CHECK(hasDNSSeeds || hasFixedSeeds);
}

BOOST_AUTO_TEST_SUITE_END()
