#ifndef MINING_H
#define MINING_H

#include "main_const.h"

/** Minimum nCoinAge required to stake PoS */
static const unsigned int nStakeMinAge = 2 / 60; // 30 minutes
/** Time to elapse before new modifier is computed */
static const unsigned int nModifierInterval = 2 * 60;
/** Genesis block subsidy */
static const int64_t nGenesisBlockReward = 1 * COIN;
/** Reserve block subsidy */
static const int64_t nBlockRewardReserve = 80000000 * COIN; // Reserve for swap from XDN cryptonote codebase
/** Standard block subsidy */
static const int64_t nBlockStandardReward = 300 * COIN;
/** Block spacing preferred */
static const int64_t BLOCK_SPACING = 120;
/** Block spacing minimum */
static const int64_t BLOCK_SPACING_MIN = 45;
/** Block spacing maximum */
static const int64_t BLOCK_SPACING_MAX = 190;
/** Desired block times/spacing */
static const int64_t GetTargetSpacing = BLOCK_SPACING;
/** MNengine collateral */
static const int64_t MNengine_COLLATERAL = (1 * COIN);
/** MNengine pool values */
static const int64_t MNengine_POOL_MAX = (999 * COIN);
/** MasterNode required collateral */
inline int64_t MasternodeCollateral(int nHeight) { return 2000000; } // 2 Million XDN required as collateral
/** Coinbase transaction outputs can only be staked after this number of new blocks (network rule) */
static const int nStakeMinConfirmations = 25;
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int nCoinbaseMaturity = 15; // 15-TXs | 90-Mined

#endif // MINING_H
