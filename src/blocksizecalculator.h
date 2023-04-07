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
