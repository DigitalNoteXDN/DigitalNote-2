#include "compat.h"

#include "util.h"
#include "cdnsseeddata.h"
#include "chainparamsseeds.h"
#include "cmainparams.h"
#include "ctestnetparams.h"
#include "cregtestparams.h"
#include "ctxin.h"
#include "ctxout.h"
#include "caddress.h"
#include "ctransaction.h"

#include "chainparams.h"

static CMainParams mainParams;
static CTestNetParams testNetParams;
static CRegTestParams regTestParams;
static CChainParams *pCurrentParams = &mainParams;

const CChainParams &Params()
{
    return *pCurrentParams;
}

void SelectParams(CChainParams_Network network)
{
    switch (network)
	{
		case CChainParams_Network::MAIN:
			pCurrentParams = &mainParams;
		break;

		case CChainParams_Network::TESTNET:
			pCurrentParams = &testNetParams;
		break;

		case CChainParams_Network::REGTEST:
			pCurrentParams = &regTestParams;
		break;

		default:
			assert(false && "Unimplemented network");
		
			return;
    }
}

bool SelectParamsFromCommandLine()
{
    bool fRegTest = GetBoolArg("-regtest", false);
    bool fTestNet = GetBoolArg("-testnet", false);

    if (fTestNet && fRegTest)
	{
        return false;
    }

    if (fRegTest)
	{
        SelectParams(CChainParams_Network::REGTEST);
    }
	else if (fTestNet)
	{
        SelectParams(CChainParams_Network::TESTNET);
    }
	else
	{
        SelectParams(CChainParams_Network::MAIN);
    }
	
    return true;
}

bool TestNet()
{
    // Note: it's deliberate that this returns "false" for regression test mode.
    return Params().NetworkID() == CChainParams_Network::TESTNET;
}

