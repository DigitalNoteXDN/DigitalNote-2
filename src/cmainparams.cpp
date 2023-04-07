#include "compat.h"

#include <boost/assign/list_of.hpp>

#include "util.h"
#include "ctxin.h"
#include "ctxout.h"
#include "main_const.h"
#include "genesis.h"
#include "chainparamsseeds.h"
#include "cdnsseeddata.h"
#include "caddress.h"
#include "ctransaction.h"
#include "fork.h"

#include "cmainparams.h"

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

CMainParams::CMainParams()
{
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
	
	bnProofOfWorkLimit = CBigNum(~uint256(0) >> 14);
	bnProofOfStakeLimit = CBigNum(~uint256(0) >> 16);

	const char* pszTimestamp = "Elon Musk Wants to Embed AI-on-a-Chip Into Every Human Brain | JP Buntinx | January 18, 2019 | News, Technology | TheMerkle";
	std::vector<CTxIn> vin;
	vin.resize(1);
	vin[0].scriptSig = CScript() << 0 << CBigNum(42) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
	std::vector<CTxOut> vout;
	vout.resize(1);
	vout[0].nValue = 1 * COIN;
	vout[0].SetEmpty();
	CTransaction txNew(1, timeGenesisBlock, vin, vout, 0);
	genesis.vtx.push_back(txNew);
	genesis.hashPrevBlock = 0;
	genesis.hashMerkleRoot = genesis.BuildMerkleTree();
	genesis.nVersion = 1;
	genesis.nTime = timeGenesisBlock; // Sat, December 15, 2018 8:00:00 PM
	genesis.nBits = bnProofOfWorkLimit.GetCompact();
	genesis.nNonce = 14180;

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

	base58Prefixes[CChainParams_Base58Type::PUBKEY_ADDRESS] = std::vector<unsigned char>(1,90);
	base58Prefixes[CChainParams_Base58Type::SCRIPT_ADDRESS] = std::vector<unsigned char>(1,140);
	base58Prefixes[CChainParams_Base58Type::SECRET_KEY] = std::vector<unsigned char>(1,142);
	base58Prefixes[CChainParams_Base58Type::STEALTH_ADDRESS] = std::vector<unsigned char>(1,115);
	base58Prefixes[CChainParams_Base58Type::EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
	base58Prefixes[CChainParams_Base58Type::EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();

	vSeeds.push_back(CDNSSeedData("node0",  "103.164.54.203"));
	vSeeds.push_back(CDNSSeedData("node1",  "192.241.147.56"));
	vSeeds.push_back(CDNSSeedData("node2",  "20.193.89.74"));
	vSeeds.push_back(CDNSSeedData("node3",  "161.97.92.102"));
	vSeeds.push_back(CDNSSeedData("node4",  "161.97.106.85"));
	vSeeds.push_back(CDNSSeedData("node5",  "62.171.150.246"));
	vSeeds.push_back(CDNSSeedData("node6",  "138.197.161.183"));
	vSeeds.push_back(CDNSSeedData("node7",  "157.230.107.144"));
	vSeeds.push_back(CDNSSeedData("node8",  "188.166.123.46"));
	vSeeds.push_back(CDNSSeedData("node9",  "159.203.14.113"));
	vSeeds.push_back(CDNSSeedData("node10", "199.175.54.187"));
	vSeeds.push_back(CDNSSeedData("node11", "seed1n.digitalnote.biz"));
	vSeeds.push_back(CDNSSeedData("node12", "seed2n.digitalnote.biz"));
	vSeeds.push_back(CDNSSeedData("node13", "seed3n.digitalnote.biz"));
	vSeeds.push_back(CDNSSeedData("node14", "seed4n.digitalnote.biz"));

	convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

	nPoolMaxTransactions = 9;
	strMNenginePoolDummyAddress = "dUnVN6zz2apaoWkb5krGoBCwwo8ZD3axue";
	strDevOpsAddress = VERION_1_0_4_2_DEVELOPER_ADDRESS;
	nEndPoWBlock = 0x7fffffff;
	nStartPoSBlock = 0;
}

const CBlock& CMainParams::GenesisBlock() const
{
	return genesis;
}

CChainParams_Network CMainParams::NetworkID() const
{
	return CChainParams_Network::MAIN;
}

const std::vector<CAddress>& CMainParams::FixedSeeds() const
{
	return vFixedSeeds;
}

