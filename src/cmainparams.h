#ifndef CMAINPARAMS_H
#define CMAINPARAMS_H

#include <vector>

#include "cchainparams.h"
#include "cblock.h"

class CAddress;

//
// Main network
//
class CMainParams : public CChainParams
{
protected:
	CBlock genesis;
	std::vector<CAddress> vFixedSeeds;

public:
	CMainParams();

	virtual const CBlock& GenesisBlock() const;
	virtual CChainParams_Network NetworkID() const;
	virtual const std::vector<CAddress>& FixedSeeds() const;
};

#endif // CMAINPARAMS_H
