#ifndef MAIN_EXTERN_H
#define MAIN_EXTERN_H

#include <set>
#include "types/ccriticalsection.h"
#include "cmainsignals.h"

class CScript;
class CTxMemPool;
class CBlockIndex;
class uint256;
class COutPoint;
struct COrphanBlock;

extern CScript COINBASE_FLAGS;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
extern std::map<uint256, CBlockIndex*> mapBlockIndex;
extern std::set<std::pair<COutPoint, unsigned int> > setStakeSeen;
extern CBlockIndex* pindexGenesisBlock;
extern unsigned int nNodeLifespan;
extern int nBestHeight;
extern uint256 nBestChainTrust;
extern uint256 nBestInvalidTrust;
extern uint256 hashBestChain;
extern CBlockIndex* pindexBest;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern int64_t nLastCoinStakeSearchInterval;
extern const std::string strMessageMagic;
extern int64_t nTimeBestReceived;
extern bool fImporting;
extern bool fReindex;
extern std::map<uint256, COrphanBlock*> mapOrphanBlocks;
extern bool fHaveGUI;
// Settings
extern bool fUseFastIndex;
extern unsigned int nDerivationMethodIndex;
extern bool fLargeWorkForkFound;
extern bool fLargeWorkInvalidChainFound;
extern CMainSignals g_signals;
extern CBlockIndex* pblockindexFBBHLast;

#endif // MAIN_EXTERN_H
