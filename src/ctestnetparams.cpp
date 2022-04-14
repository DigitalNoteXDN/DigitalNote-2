#include "compat.h"

#include <boost/assign/list_of.hpp>

#include "util.h"
#include "ctxin.h"
#include "ctxout.h"
#include "genesis.h"
#include "cdnsseeddata.h"
#include "protocol.h"
#include "caddress.h"
#include "ctransaction.h"

#include "ctestnetparams.h"

CTestNetParams::CTestNetParams()
{
	// The message start string is designed to be unlikely to occur in normal data.
	// The characters are rarely used upper ASCII, not valid as UTF-8, and produce
	// a large 4-byte int at any alignment.
	pchMessageStart[0] = 0x42;
	pchMessageStart[1] = 0xbc;
	pchMessageStart[2] = 0x1c;
	pchMessageStart[3] = 0xf4;
	bnProofOfWorkLimit = CBigNum(~uint256(0) >> 12);
	bnProofOfStakeLimit = CBigNum(~uint256(0) >> 14);
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

	base58Prefixes[CChainParams_Base58Type::PUBKEY_ADDRESS] = std::vector<unsigned char>(1,91);
	base58Prefixes[CChainParams_Base58Type::SCRIPT_ADDRESS] = std::vector<unsigned char>(1,100);
	base58Prefixes[CChainParams_Base58Type::SECRET_KEY] = std::vector<unsigned char>(1,102);
	base58Prefixes[CChainParams_Base58Type::STEALTH_ADDRESS] = std::vector<unsigned char>(1,106);
	base58Prefixes[CChainParams_Base58Type::EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
	base58Prefixes[CChainParams_Base58Type::EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

	nEndPoWBlock = 0x7fffffff;
}

CChainParams_Network CTestNetParams::NetworkID() const
{
	return CChainParams_Network::TESTNET;
}

