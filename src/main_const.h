#ifndef MAIN_CONST_H
#define MAIN_CONST_H

#include <cstdint>

static const int64_t COIN = 100000000;
static const int64_t CENT = 1000000;

/** The maximum allowed multiple for the computed block size */
static const unsigned int MAX_BLOCK_SIZE_INCREASE_MULTIPLE = 2;
/** The number of blocks to consider in the computation of median block size */
static const unsigned int NUM_BLOCKS_FOR_MEDIAN_BLOCK = 25;
/** The maximum allowed size for a serialized block, in bytes (network rule) */
static unsigned int MAX_BLOCK_SIZE = 15256128;
/** The minimum allowed size for a serialized block, in bytes (network rule) */
static const unsigned int MIN_BLOCK_SIZE = 1525612;
/** The maximum size for mined blocks */
static const unsigned int MAX_BLOCK_SIZE_GEN = MAX_BLOCK_SIZE/2;
/** Default for -blockprioritysize, maximum space for zero/low-fee transactions **/
static const unsigned int DEFAULT_BLOCK_PRIORITY_SIZE = 50000;
/** The maximum size for transactions we're willing to relay/mine **/
static const unsigned int MAX_STANDARD_TX_SIZE = MAX_BLOCK_SIZE_GEN/5;
/** The maximum allowed number of signature check operations in a block (network rule) */
static unsigned int MAX_BLOCK_SIGOPS = MAX_BLOCK_SIZE/50;
/** Maxiumum number of signature check operations in an IsStandard() P2SH script */
static const unsigned int MAX_P2SH_SIGOPS = 15;
/** The maximum number of sigops we're willing to relay/mine in a single tx */
static unsigned int MAX_TX_SIGOPS = MAX_BLOCK_SIGOPS/5;
/** The maximum number of orphan transactions kept in memory */
static const unsigned int MAX_ORPHAN_TRANSACTIONS = MAX_BLOCK_SIZE/100;
/** Default for -maxorphanblocks, maximum number of orphan blocks kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_BLOCKS = 10000;
/** Fees smaller than this (in satoshi) are considered zero fee (for transaction creation) */
static const int64_t MIN_TX_FEE = 0.0001*COIN;
/** Fees smaller than this (in satoshi) are considered zero fee (for relaying) */
static const int64_t MIN_RELAY_TX_FEE = MIN_TX_FEE;
/** Minimum TX count (for relaying) */
static const int64_t MIN_TX_COUNT = 0;
/** Minimum TX value (for relaying) */
static const int64_t MIN_TX_VALUE = 0.01 * COIN;
/** No amount larger than this (in satoshi) is valid */
static const int64_t MAX_SINGLE_TX = 10000000000 * COIN; // 10 Billion DigitalNote coins
/** Threshold for nLockTime: below this value it is interpreted as block number, otherwise as UNIX timestamp. */
static const unsigned int LOCKTIME_THRESHOLD = 500000000; // Tue Nov  5 00:53:20 1985 UTC
/** Number of blocks that can be requested at any given time from a single peer. */
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 128;
/** Timeout in seconds before considering a block download peer unresponsive. */
static const unsigned int BLOCK_DOWNLOAD_TIMEOUT = 60;
/** Defaults to yes, adaptively increase/decrease max/min/priority along with the re-calculated block size **/
static const unsigned int DEFAULT_SCALE_BLOCK_SIZE_OPTIONS = 1;
/** Future drift value */
static const int64_t nDrift = 5 * 60;
/** "reject" message codes **/
static const unsigned char REJECT_INVALID = 0x10;
// Minimum disk space required - used in CheckDiskSpace()
static const uint64_t nMinDiskSpace = 52428800;

static const unsigned int MAX_MESSAGE_SIZE = 0x02000000;

#endif // MAIN_CONST_H
