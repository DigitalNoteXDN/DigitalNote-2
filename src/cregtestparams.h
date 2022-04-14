#ifndef CREGTESTPARAMS_H
#define CREGTESTPARAMS_H

#include "ctestnetparams.h"

//
// Regression test
//
class CRegTestParams : public CTestNetParams
{
public:
	CRegTestParams();

	virtual bool RequireRPCPassword() const;
	virtual CChainParams_Network NetworkID() const;
};

#endif // CREGTESTPARAMS_H
