#ifndef INSTANTX_H
#define INSTANTX_H

#include <cstdint>
#include <string>
#include <map>

class CConsensusVote;
class CTransaction;
class CTransactionLock;
class uint256;
class COutPoint;
class CNode;
class CDataStream;

extern std::map<uint256, CTransaction> mapTxLockReq;
extern std::map<uint256, CTransaction> mapTxLockReqRejected;
extern std::map<uint256, CConsensusVote> mapTxLockVote;
extern std::map<uint256, CTransactionLock> mapTxLocks;
extern std::map<COutPoint, uint256> mapLockedInputs;
extern int nCompleteTXLocks;


int64_t CreateNewLock(CTransaction tx);
bool IsIXTXValid(const CTransaction& txCollateral);
// if two conflicting locks are approved by the network, they will cancel out
bool CheckForConflictingLocks(CTransaction& tx);
void ProcessMessageInstantX(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
//check if we need to vote on this transaction
void DoConsensusVote(CTransaction& tx, int64_t nBlockHeight);
//process consensus vote message
bool ProcessConsensusVote(CNode* pnode, CConsensusVote& ctx);
// keep transaction locks in memory for an hour
void CleanTransactionLocksList();
int64_t GetAverageVoteTime();

#endif // INSTANTX_H
