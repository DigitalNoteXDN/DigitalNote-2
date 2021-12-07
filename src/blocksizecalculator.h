// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKSIZECALCUALTOR_H
#define BLOCKSIZECALCUALTOR_H

#include <vector>

#include "main_const.h"

class CBlockIndex;
class CAutoFile;

namespace BlockSizeCalculator
{
    unsigned int ComputeBlockSize(CBlockIndex*, unsigned int pastblocks = NUM_BLOCKS_FOR_MEDIAN_BLOCK);
    inline unsigned int GetMedianBlockSize(CBlockIndex*, unsigned int pastblocks = NUM_BLOCKS_FOR_MEDIAN_BLOCK);
    inline std::vector<unsigned int> GetBlockSizes(CBlockIndex*, unsigned int pastblocks = NUM_BLOCKS_FOR_MEDIAN_BLOCK);
    inline int GetBlockSize(CBlockIndex*);
}

#endif // BLOCKSIZECALCUALTOR_H
