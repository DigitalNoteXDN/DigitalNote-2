#ifndef CTESTNETPARAMS_H
#define CTESTNETPARAMS_H

#include "cmainparams.h"

//
// Testnet
//
class CTestNetParams : public CMainParams
{
public:
	CTestNetParams();

	virtual CChainParams_Network NetworkID() const;
};

#endif // CTESTNETPARAMS_H
