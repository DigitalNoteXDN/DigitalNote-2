#ifndef CRPCCOMMAND_H
#define CRPCCOMMAND_H

#include <string>

#include "types/rpcfn_type.h"

class CRPCCommand
{
public:
	std::string name;
	rpcfn_type actor;
	bool okSafeMode;
	bool threadSafe;
	bool reqWallet;
};

#endif // CRPCCOMMAND_H
