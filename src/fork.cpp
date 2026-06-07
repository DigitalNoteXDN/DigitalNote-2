#include "main_extern.h"
#include "cblockindex.h"
#include "chainparams.h"

#include "fork.h"

// v2.0.0.8 CW9: pure-height variant of the devops ladder lookup.
//
// Caller passes the height of the block whose devops payee is being
// determined.  Producer callers pass `pindexPrev->nHeight + 1` (the
// height of the block being constructed).  Validator callers pass
// `pindex->nHeight` (the height of the block being validated).
//
// Both producer and validator therefore ask the ladder about the SAME
// block, eliminating the longstanding off-by-one where producer code
// asked about pindexBest (the tip, = N-1) when building block N while
// validator code asked about pindex (= N).
//
// The new v2.0.1.0 boundary uses < (strictly less), so block at exactly
// VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK returns the NEW address.  The
// pre-existing v1.0.4.2 boundary uses <= for backward compatibility
// with chain history that was sealed under the previous off-by-one
// behaviour (do not change to preserve canonical validation).
std::string getDevelopersAdressForHeight(int nHeight, int64_t nBlockTime)
{
	if(TestNet())
	{
		// Testnet ladder.  Pre-rotation uses the bootstrap address;
		// post-rotation (block 100 onwards on testnet) uses the
		// v2.0.1.0 testnet address.
		if(nHeight < VERION_2_0_1_0_TESTNET_UPDATE_BLOCK)
		{
			return TESTNET_DEVELOPER_ADDRESS;
		}
		return VERION_2_0_1_0_TESTNET_DEVELOPER_ADDRESS;
	}

	// Mainnet ladder, oldest-to-newest.
	//
	// Pre-v1.0.1.5: time-based boundary (preserved verbatim from
	// pre-CW9 code; do not change -- chain history depends on it).
	if(nBlockTime < VERION_1_0_1_5_MANDATORY_UPDATE_START)
	{
		return VERION_1_0_0_0_DEVELOPER_ADDRESS;
	}
	// Pre-v1.0.4.2: height-based with <= boundary (preserved verbatim).
	// Block at exactly VERION_1_0_4_2_MANDATORY_UPDATE_BLOCK (= 403117)
	// returns the v1.0.1.5 address.  Empirically, mainnet chain history
	// at block 403117 actually paid the v1.0.4.2 address; the lax
	// pre-rotation validator in CheckBlock absorbs that discrepancy.
	else if(nHeight <= VERION_1_0_4_2_MANDATORY_UPDATE_BLOCK)
	{
		return VERION_1_0_1_5_DEVELOPER_ADDRESS;
	}
	// Pre-v2.0.1.0: post-v1.0.4.2 era (current era as of v2.0.0.8 ship).
	// Note the < boundary: block at exactly
	// VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK returns the NEW (v2.0.1.0)
	// address.  This is the rotation activation block itself.
	else if(nHeight < VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK)
	{
		return VERION_1_0_4_2_DEVELOPER_ADDRESS;
	}

	// v2.0.1.0 era (post-rotation).
	return VERION_2_0_1_0_DEVELOPER_ADDRESS;
}

// Backward-compatible wrapper.  Equivalent to calling the height-based
// variant with the index's own nHeight + GetBlockTime().  Validator code
// uses this freely (it already operates on pindex of the block being
// validated, which is correct).
std::string getDevelopersAdress(const CBlockIndex* pindex)
{
	return getDevelopersAdressForHeight(pindex->nHeight, pindex->GetBlockTime());
}

