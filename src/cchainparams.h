#ifndef CCHAINPARAMS_H
#define CCHAINPARAMS_H

#include <vector>
#include <string>

#include "uint/uint256.h"
#include "cbignum.h"
#include "message_start_size.h"
#include "enums/cchainparams_network.h"
#include "enums/cchainparams_base58type.h"

struct CDNSSeedData;
class CBlock;
class CAddress;

typedef unsigned char MessageStartChars[MESSAGE_START_SIZE];

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * DigitalNote system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams
{
protected:
	uint256 hashGenesisBlock;
	MessageStartChars pchMessageStart;
	// Raw pub key bytes for the broadcast alert signing key.
	std::vector<unsigned char> vAlertPubKey;
	int nDefaultPort;
	int nRPCPort;
	CBigNum bnProofOfWorkLimit;
	CBigNum bnProofOfStakeLimit;
	std::string strDataDir;
	std::vector<CDNSSeedData> vSeeds;
	std::vector<unsigned char> base58Prefixes[CChainParams_Base58Type::MAX_BASE58_TYPES];
	int nEndPoWBlock;
	int nStartPoSBlock;
	int nPoolMaxTransactions;
	std::string strMNenginePoolDummyAddress;
	std::string strDevOpsAddress;

public:
	virtual const CBlock& GenesisBlock() const = 0;
	virtual CChainParams_Network NetworkID() const = 0;
	virtual const std::vector<CAddress>& FixedSeeds() const = 0;

protected:
	CChainParams();

public:
	const uint256& HashGenesisBlock() const;
	const MessageStartChars& MessageStart() const;
	const std::vector<unsigned char>& AlertKey() const;
	int GetDefaultPort() const;
	const CBigNum& ProofOfWorkLimit() const;
	const CBigNum& ProofOfStakeLimit() const;
	virtual bool RequireRPCPassword() const;
	const std::string& DataDir() const;
	const std::vector<CDNSSeedData>& DNSSeeds() const;
	const std::vector<unsigned char>& Base58Prefix(CChainParams_Base58Type type) const;
	int RPCPort() const;
	int EndPoWBlock() const;
	int StartPoSBlock() const;
	int PoolMaxTransactions() const;
	std::string MNenginePoolDummyAddress() const;
	std::string DevOpsAddress() const;
};

#endif // CCHAINPARAMS_H
