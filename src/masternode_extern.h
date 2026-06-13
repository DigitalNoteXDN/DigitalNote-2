#ifndef MASTERNODE_EXTERN_H
#define MASTERNODE_EXTERN_H

#include <cstdint>
#include <map>

#include "types/ccriticalsection.h"

class uint256;
class CMasternodeMan;
class CMasternodePayments;
class CMasternodePaymentWinner;

extern CCriticalSection cs_masternodes;
extern std::map<int64_t, uint256> mapCacheBlockHashes;
extern CMasternodeMan mnodeman;
extern CCriticalSection cs_masternodepayments;
extern CMasternodePayments masternodePayments;
extern std::map<uint256, CMasternodePaymentWinner> mapSeenMasternodeVotes;

#endif // MASTERNODE_EXTERN_H
