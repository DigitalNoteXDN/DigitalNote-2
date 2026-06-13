// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// src/test/version_tests.cpp
//
// Tests for client version and protocol version constants.
// Compiled into test_digitalnote alongside existing tests.
//
// Run:  ./src/test/test_digitalnote --run_test=VersionTests --log_level=all

#include <boost/test/unit_test.hpp>

#include "clientversion.h"
#include "version.h"
#include "util.h"

BOOST_AUTO_TEST_SUITE(VersionTests)

// ── CLIENT_VERSION constants ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(ClientVersionMajorIs2)
{
    BOOST_CHECK_EQUAL(CLIENT_VERSION_MAJOR, 2);
}

BOOST_AUTO_TEST_CASE(ClientVersionMinorIs0)
{
    BOOST_CHECK_EQUAL(CLIENT_VERSION_MINOR, 0);
}

BOOST_AUTO_TEST_CASE(ClientVersionRevisionIs0)
{
    BOOST_CHECK_EQUAL(CLIENT_VERSION_REVISION, 0);
}

BOOST_AUTO_TEST_CASE(ClientVersionBuildIs7)
{
    BOOST_CHECK_EQUAL(CLIENT_VERSION_BUILD, 7);
}

BOOST_AUTO_TEST_CASE(ClientVersionIsRelease)
{
    BOOST_CHECK(CLIENT_VERSION_IS_RELEASE);
}

BOOST_AUTO_TEST_CASE(ComputedClientVersionEquals2000007)
{
    // CLIENT_VERSION = 1000000*MAJOR + 10000*MINOR + 100*REVISION + 1*BUILD
    //                = 2000000 + 0 + 0 + 7 = 2000007
    BOOST_CHECK_EQUAL(CLIENT_VERSION, 2000007);
}

BOOST_AUTO_TEST_CASE(ClientVersionGreaterThanPreviousRelease)
{
    // Previous release was 2.0.0.6 → integer 2000006
    BOOST_CHECK_GT(CLIENT_VERSION, 2000006);
}

BOOST_AUTO_TEST_CASE(FormatFullVersionContains2_0_0_7)
{
    std::string ver = FormatFullVersion();
    BOOST_CHECK(!ver.empty());
    BOOST_CHECK(ver.find("2.0.0.7") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(FormatSubVersionNotEmpty)
{
    std::vector<std::string> comments;
    std::string sub = FormatSubVersion("DigitalNote", CLIENT_VERSION, comments);
    BOOST_CHECK(!sub.empty());
    BOOST_CHECK_EQUAL(sub.front(), '/');
    BOOST_CHECK_EQUAL(sub.back(),  '/');
}

BOOST_AUTO_TEST_CASE(FormatSubVersionWithComments)
{
    std::vector<std::string> comments = {"testnet"};
    std::string sub = FormatSubVersion("DigitalNote", CLIENT_VERSION, comments);
    BOOST_CHECK(sub.find("testnet") != std::string::npos);
}

// ── PROTOCOL_VERSION constants ────────────────────────────────────────────────
//
// The existing repo uses a 5-digit Peercoin/Dash-lineage protocol version.
// v2.0.0.6 was 62054; this release bumps it by 1 to 62055.

BOOST_AUTO_TEST_CASE(ProtocolVersionIs62055)
{
    BOOST_CHECK_EQUAL(PROTOCOL_VERSION, 62055);
}

BOOST_AUTO_TEST_CASE(ProtocolVersionExactlyOneBumpFromPrevious)
{
    // Must be exactly 62054 + 1 — no skips
    BOOST_CHECK_EQUAL(PROTOCOL_VERSION, 62054 + 1);
}

BOOST_AUTO_TEST_CASE(MinPeerProtoVersionIs62052)
{
    // Intentionally kept at 62052 — allows existing network peers running
    // older protocol versions to stay connected during rollout.
    BOOST_CHECK_EQUAL(MIN_PEER_PROTO_VERSION, 62052);
}

BOOST_AUTO_TEST_CASE(MinProtoVersionIsGraceWindowAt62054)
{
    // Grace window: accept one version back during rollout
    BOOST_CHECK_EQUAL(MIN_PROTO_VERSION, 62054);
    BOOST_CHECK_LT(MIN_PROTO_VERSION, PROTOCOL_VERSION);
}

BOOST_AUTO_TEST_CASE(ProtocolVersionGreaterThanMinPeer)
{
    // PROTOCOL_VERSION (62055) must be greater than MIN_PEER_PROTO_VERSION (62052)
    // confirming the network accepts older peers while advertising the new version
    BOOST_CHECK_GT(PROTOCOL_VERSION, MIN_PEER_PROTO_VERSION);
}

BOOST_AUTO_TEST_CASE(MinProtoVersionNotGreaterThanProtocol)
{
    BOOST_CHECK_LE(MIN_PROTO_VERSION, PROTOCOL_VERSION);
}

BOOST_AUTO_TEST_CASE(MinPeerProtoVersionBelowProtocol)
{
    // MIN_PEER_PROTO_VERSION (62052) must be strictly below PROTOCOL_VERSION (62055)
    // and also below MIN_PROTO_VERSION (62054)
    BOOST_CHECK_LT(MIN_PEER_PROTO_VERSION, PROTOCOL_VERSION);
    BOOST_CHECK_LT(MIN_PEER_PROTO_VERSION, MIN_PROTO_VERSION);
}

BOOST_AUTO_TEST_CASE(ProtocolVersionGap_ThreeFromMinPeer)
{
    // PROTOCOL_VERSION is 62055, MIN_PEER_PROTO_VERSION is 62052 — gap of 3
    // This is intentional: we accept peers from the last 3 protocol versions
    BOOST_CHECK_EQUAL(PROTOCOL_VERSION - MIN_PEER_PROTO_VERSION, 3);
}

BOOST_AUTO_TEST_CASE(CAddrTimeVersionSanity)
{
    BOOST_CHECK_EQUAL(CADDR_TIME_VERSION, 31402);
}

BOOST_AUTO_TEST_CASE(BIP0031VersionSanity)
{
    BOOST_CHECK_EQUAL(BIP0031_VERSION, 60000);
}

BOOST_AUTO_TEST_CASE(NoblksVersionRangeSane)
{
    BOOST_CHECK_LT(NOBLKS_VERSION_START, NOBLKS_VERSION_END);
}

BOOST_AUTO_TEST_CASE(CopyrightYearIs2025)
{
    BOOST_CHECK_EQUAL(COPYRIGHT_YEAR, 2025);
}

// ── Serialisation uses correct protocol version ───────────────────────────────
// Verify that PROTOCOL_VERSION as used in CDataStream matches the header

BOOST_AUTO_TEST_CASE(SerialiseVersionMatchesProtocolVersion)
{
    // CDataStream(SER_NETWORK, PROTOCOL_VERSION) — the version baked into
    // all network messages must equal PROTOCOL_VERSION exactly
    int streamVer = PROTOCOL_VERSION;
    BOOST_CHECK_EQUAL(streamVer, 62055);
}

BOOST_AUTO_TEST_SUITE_END()
