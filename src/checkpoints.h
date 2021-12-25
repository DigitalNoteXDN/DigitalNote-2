#ifndef CHECKPOINTS_H
#define CHECKPOINTS_H

#include <map>
#include "util.h"

class uint256;
class CBlockIndex;

/** Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
namespace Checkpoints
{
	typedef std::map<int, uint256> MapCheckpoints;

    // Returns true if block passes checkpoint checks
    bool CheckHardened(int nHeight, const uint256& hash);

    // Return conservative estimate of total number of blocks, 0 if unknown
    int GetTotalBlocksEstimate();

    // Returns last CBlockIndex* in mapBlockIndex that is a checkpoint
    CBlockIndex* GetLastCheckpoint(const std::map<uint256, CBlockIndex*>& mapBlockIndex);

    const CBlockIndex* AutoSelectSyncCheckpoint();
    bool CheckSync(int nHeight);
}

#endif // CHECKPOINTS_H
