// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assert.h"

#include "chainparams.h"
#include "random.h"
#include "main.h"
#include "util.h"

#include <boost/assign/list_of.hpp>

using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

// Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress> &vSeedsOut, const SeedSpec6 *data, unsigned int count)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7*24*60*60;
    for (unsigned int i = 0; i < count; i++)
    {
        struct in6_addr ip;
        memcpy(&ip, data[i].addr, sizeof(ip));
        CAddress addr(CService(ip, data[i].port));
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
}

//
// Main network
//
class CMainParams : public CChainParams {
public:
    CMainParams() {
        // The message start string is designed to be unlikely to occur in normal data.
        // The characters are rarely used upper ASCII, not valid as UTF-8, and produce
        // a large 4-byte int at any alignment.
        pchMessageStart[0] = 0x21;
        pchMessageStart[1] = 0xaf;
        pchMessageStart[2] = 0x9c;
        pchMessageStart[3] = 0xe3;
        vAlertPubKey = ParseHex("01b88735a49f1996be6b659c91a94fbfebeb5d517698712acdbef262f7c2f81f85d131a669df3be611393f454852a2d08c6314bba5ca3cbe5616262da3b1a6afed");
        nDefaultPort = 18092;
        nRPCPort = 18094;
        bnProofOfWorkLimit = ~uint256(0) >> 14;
        bnProofOfStakeLimit = ~uint256(0) >> 16;

        const char* pszTimestamp = "Elon Musk Wants to Embed AI-on-a-Chip Into Every Human Brain | JP Buntinx | January 18, 2019 | News, Technology | TheMerkle";
        std::vector<CTxIn> vin;
        vin.resize(1);
        vin[0].scriptSig = CScript() << 0 << 42 << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        std::vector<CTxOut> vout;
        vout.resize(1);
        vout[0].nValue = 1 * COIN;
        vout[0].SetEmpty();
        CTransaction txNew(1, timeGenesisBlock, vin, vout, 0);
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime    = timeGenesisBlock; // Sat, December 15, 2018 8:00:00 PM
        genesis.nBits    = bnProofOfWorkLimit.GetCompact();
        genesis.nNonce   = 14180;

        /** Genesis Block MainNet */
        /*
            Hashed MainNet Genesis Block Output
            block.hashMerkleRoot == 3b9d152cb1370d54d1ea30d5e334a83a41ca9403011495b8743a53d53423004a
            block.nTime = 1547848800
            block.nNonce = 14180
            block.GetHash = 00000d8e7d39218c4c02132e95a3896d46939b9b95624cf9dd2b0b794e6c216a
        */

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x00000d8e7d39218c4c02132e95a3896d46939b9b95624cf9dd2b0b794e6c216a"));
        assert(genesis.hashMerkleRoot == uint256("0x3b9d152cb1370d54d1ea30d5e334a83a41ca9403011495b8743a53d53423004a"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,90);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,140);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,142);
        base58Prefixes[STEALTH_ADDRESS] = std::vector<unsigned char>(1,115);
        base58Prefixes[EXT_PUBLIC_KEY] = list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();

        vSeeds.push_back(CDNSSeedData("node0",  "206.189.183.68"));
        vSeeds.push_back(CDNSSeedData("node1",  "167.71.183.195"));
        vSeeds.push_back(CDNSSeedData("node2",  "142.93.201.211"));
        vSeeds.push_back(CDNSSeedData("node3",  "134.209.55.249"));
        vSeeds.push_back(CDNSSeedData("node4",  "seed1n.digitalnote.biz"));
        vSeeds.push_back(CDNSSeedData("node5",  "seed2n.digitalnote.biz"));
        vSeeds.push_back(CDNSSeedData("node6",  "seed3n.digitalnote.biz"));
        vSeeds.push_back(CDNSSeedData("node7",  "seed4n.digitalnote.biz"));

        convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        nPoolMaxTransactions = 9;
        strMNenginePoolDummyAddress = "dUnVN6zz2apaoWkb5krGoBCwwo8ZD3axue";
        strDevOpsAddress = "dSCXLHTZJJqTej8ZRszZxbLrS6dDGVJhw7";
        nEndPoWBlock = 0x7fffffff;
        nStartPoSBlock = 0;
    }

    virtual const CBlock& GenesisBlock() const { return genesis; }
    virtual Network NetworkID() const { return CChainParams::MAIN; }

    virtual const vector<CAddress>& FixedSeeds() const {
        return vFixedSeeds;
    }
protected:
    CBlock genesis;
    vector<CAddress> vFixedSeeds;
};
static CMainParams mainParams;


//
// Testnet
//
class CTestNetParams : public CMainParams {
public:
    CTestNetParams() {
        // The message start string is designed to be unlikely to occur in normal data.
        // The characters are rarely used upper ASCII, not valid as UTF-8, and produce
        // a large 4-byte int at any alignment.
        pchMessageStart[0] = 0x42;
        pchMessageStart[1] = 0xbc;
        pchMessageStart[2] = 0x1c;
        pchMessageStart[3] = 0xf4;
        bnProofOfWorkLimit = ~uint256(0) >> 12;
        bnProofOfStakeLimit = ~uint256(0) >> 14;
        vAlertPubKey = ParseHex("00f88735a49f1996be6b659c91a94fbfebeb5d517698712acdbef262f7c2f81f85d131a669df3be611393f454852a2d08c6314bba5ca3cbe5616262da3b1a6afed");
        nDefaultPort = 28092;
        nRPCPort = 28094;
        strDataDir = "testnet";

        // Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nTime  = timeTestNetGenesis;
        genesis.nBits  = bnProofOfWorkLimit.GetCompact();
        genesis.nNonce = 16793;

        /** Genesis Block TestNet */
        /*
            Hashed TestNet Genesis Block Output
            block.hashMerkleRoot == 3b9d152cb1370d54d1ea30d5e334a83a41ca9403011495b8743a53d53423004a
            block.nTime = 1547848830
            block.nNonce = 16793
            block.GetHash = 000510a669c8d36db04317fa98f7bf183d18c96cef5a4a94a6784a2c47f92e6c
        */

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x000510a669c8d36db04317fa98f7bf183d18c96cef5a4a94a6784a2c47f92e6c"));

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,91);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,100);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,102);
        base58Prefixes[STEALTH_ADDRESS] = std::vector<unsigned char>(1,106);
        base58Prefixes[EXT_PUBLIC_KEY] = list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        nEndPoWBlock = 0x7fffffff;
    }
    virtual Network NetworkID() const { return CChainParams::TESTNET; }
};
static CTestNetParams testNetParams;

//
// Regression test
//
class CRegTestParams : public CTestNetParams {
public:
    CRegTestParams() {
        pchMessageStart[0] = 0x11;
        pchMessageStart[1] = 0xbb;
        pchMessageStart[2] = 0x0a;
        pchMessageStart[3] = 0xa9;
        bnProofOfWorkLimit = ~uint256(0) >> 1;
        genesis.nTime = timeRegNetGenesis;
        genesis.nBits  = bnProofOfWorkLimit.GetCompact();
        genesis.nNonce = 8;
        hashGenesisBlock = genesis.GetHash();
        nDefaultPort = 38883;
        strDataDir = "regtest";

        /** Genesis Block RegNet */
        /*
            Hashed RegNet Genesis Block Output
            block.hashMerkleRoot == 3b9d152cb1370d54d1ea30d5e334a83a41ca9403011495b8743a53d53423004a
            block.nTime = 1547848890
            block.nNonce = 8
            block.GetHash = 4ca84dc9b0f84d9058ec5b57ef066ebac8cad4e0355e16c8643c8c4ce6d4e071
        */

        assert(hashGenesisBlock == uint256("0x4ca84dc9b0f84d9058ec5b57ef066ebac8cad4e0355e16c8643c8c4ce6d4e071"));

        vSeeds.clear();  // Regtest mode doesn't have any DNS seeds.
    }

    virtual bool RequireRPCPassword() const { return false; }
    virtual Network NetworkID() const { return CChainParams::REGTEST; }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = &mainParams;

const CChainParams &Params() {
    return *pCurrentParams;
}

void SelectParams(CChainParams::Network network) {
    switch (network) {
        case CChainParams::MAIN:
            pCurrentParams = &mainParams;
            break;
        case CChainParams::TESTNET:
            pCurrentParams = &testNetParams;
            break;
        case CChainParams::REGTEST:
            pCurrentParams = &regTestParams;
            break;
        default:
            assert(false && "Unimplemented network");
            return;
    }
}

bool SelectParamsFromCommandLine() {
    bool fRegTest = GetBoolArg("-regtest", false);
    bool fTestNet = GetBoolArg("-testnet", false);

    if (fTestNet && fRegTest) {
        return false;
    }

    if (fRegTest) {
        SelectParams(CChainParams::REGTEST);
    } else if (fTestNet) {
        SelectParams(CChainParams::TESTNET);
    } else {
        SelectParams(CChainParams::MAIN);
    }
    return true;
}
