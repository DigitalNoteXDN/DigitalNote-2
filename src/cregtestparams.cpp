#include "compat.h"

#include "ctxin.h"
#include "ctxout.h"
#include "genesis.h"
#include "cdnsseeddata.h"
#include "caddress.h"
#include "ctransaction.h"

#include "cregtestparams.h"

CRegTestParams::CRegTestParams()
{
	pchMessageStart[0] = 0x11;
	pchMessageStart[1] = 0xbb;
	pchMessageStart[2] = 0x0a;
	pchMessageStart[3] = 0xa9;
	bnProofOfWorkLimit = CBigNum(~uint256(0) >> 1);
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

bool CRegTestParams::RequireRPCPassword() const
{
	return false;
}

CChainParams_Network CRegTestParams::NetworkID() const
{
	return CChainParams_Network::REGTEST;
}

