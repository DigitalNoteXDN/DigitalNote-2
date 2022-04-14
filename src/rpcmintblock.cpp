#include "rpcprotocol.h"
#include "net.h"
#include "net/cnodestats.h"
#include "cblock.h"
#include "ctransaction.h"
#include "ctxout.h"
#include "ctxin.h"
#include "main_const.h"
#include "cpubkey.h"
#include "init.h"
#include "cwallet.h"
#include "cmasternodepayments.h"
#include "util.h"
#include "chainparams.h"
#include "cchainparams.h"
#include "cbignum.h"
#include "main_extern.h"
#include "cblockindex.h"
#include "miner.h"
#include "creservekey.h"

json_spirit::Value mintblock(const json_spirit::Array& params, bool fHelp)
{
	CBlock* block;
	CReserveKey* pMiningKey = NULL;

	pMiningKey = new CReserveKey(pwalletMain);

	block = CreateNewBlock(*pMiningKey);

	bool fAccepted = ProcessBlock(NULL, block);
	if (!fAccepted)
	{
		return "rejected";
	}

	return block->ToString();
}




/*
// Example
json_spirit::Value mintblock(const json_spirit::Array& params, bool fHelp)
{
	CNodeStats stats;
	
	pnodeLocalHost->copyStats(stats);
	
	return stats.addrName;
}
*/