#ifndef SPORK_H
#define SPORK_H

#include <map>
#include <string>

#include "csporkmanager.h"

// Don't ever reuse these IDs for other sporks
#define SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT               10000
#define SPORK_2_INSTANTX                                      10001
#define SPORK_3_INSTANTX_BLOCK_FILTERING                      10002
#define SPORK_4_NOTUSED                                       10003
#define SPORK_5_MAX_VALUE                                     10004
#define SPORK_6_REPLAY_BLOCKS                                 10005
#define SPORK_7_MASTERNODE_SCANNING                           10006
#define SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT                10007
#define SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT                 10008
#define SPORK_10_MASTERNODE_PAY_UPDATED_NODES                 10009
#define SPORK_11_RESET_BUDGET                                 10010
#define SPORK_12_RECONSIDER_BLOCKS                            10011
#define SPORK_13_ENABLE_SUPERBLOCKS                           10012

#define SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT_DEFAULT       4070908800   // OFF
#define SPORK_2_INSTANTX_DEFAULT                              0            // ON
#define SPORK_3_INSTANTX_BLOCK_FILTERING_DEFAULT              0            // ON
#define SPORK_4_RECONVERGE_DEFAULT                            0            // ON - BUT NOT USED
#define SPORK_5_MAX_VALUE_DEFAULT                             3000000      // 3,000,000 XDN
#define SPORK_6_REPLAY_BLOCKS_DEFAULT                         0            // ON - BUT NOT USED
#define SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT        4070908800   // OFF
#define SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT_DEFAULT         4070908800   // OFF
#define SPORK_10_MASTERNODE_PAY_UPDATED_NODES_DEFAULT         4070908800   // OFF
#define SPORK_11_RESET_BUDGET_DEFAULT                         0            // ON
#define SPORK_12_RECONSIDER_BLOCKS_DEFAULT                    0            // ON
#define SPORK_13_ENABLE_SUPERBLOCKS_DEFAULT                   4070908800   // OFF

class CSporkMessage;
class uint256;
class CDataStream;
class CNode;

extern std::map<uint256, CSporkMessage> mapSporks;
extern std::map<int, CSporkMessage> mapSporksActive;
extern CSporkManager sporkManager;

void ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
int64_t GetSporkValue(int nSporkID);
bool IsSporkActive(int nSporkID);
void ExecuteSpork(int nSporkID, int nValue);
//void ReprocessBlocks(int nBlocks);

#endif
