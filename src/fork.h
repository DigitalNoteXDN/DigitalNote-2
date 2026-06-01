#ifndef FORK_H
#define FORK_H

#include <cstdint>
#include <string>
#include <map>

class CBlockIndex;

/** Reserve Phase start block */ 
static const int64_t nReservePhaseStart = 1;
/** Velocity toggle block */
static const int64_t VELOCITY_TOGGLE = 175; // Implementation of the Velocity system into the chain.
/** Velocity retarget toggle block */
static const int64_t VELOCITY_TDIFF = 0; // Use Velocity's retargetting method.
/** Protocol 3.0 toggle */

/*
	Early development adresses
*/
#define VERION_UNKNOWN_A_MANDATORY_UPDATE_START	0		// Unknown
#define VERION_UNKNOWN_A_MANDATORY_UPDATE_END	0		// Unknown
#define VERION_UNKNOWN_A_DEVELOPER_ADDRESS		"Dtz6UgAxwavsnxnb7jeSRj5cgERLvV8KBy"

#define VERION_UNKNOWN_B_MANDATORY_UPDATE_START	0		// Unknown
#define VERION_UNKNOWN_B_MANDATORY_UPDATE_END	0		// Unknown
#define VERION_UNKNOWN_B_DEVELOPER_ADDRESS		"dNXKdXpviJRV5asL2sPbsxizwfBoFgsRzq"

#define VERION_UNKNOWN_C_MANDATORY_UPDATE_START	0		// Unknown
#define VERION_UNKNOWN_C_MANDATORY_UPDATE_END	0		// Unknown
#define VERION_UNKNOWN_C_DEVELOPER_ADDRESS		"dPxigPi3gY3Za2crBUV2Sn2BDCrpX9eweo"

#define BURN_ADDRESS_A							"dMsop93F7hbLSA2d666tSPjB2NXSAfXpeU"
#define BURN_ADDRESS_B							"dVibZ11CVyiso4Kw3ZLAHp7Wn77dXuvq1d"
#define BURN_ADDRESS_C							"daigDQ7VxAFwmhh59HstA53KYD5a4q81N5"

/*
	Update 1.0.0:
	- Add first developer address
*/
#define VERION_1_0_0_0_MANDATORY_UPDATE_START	1558310750		// Monday, 20 May 2019 00:00:00 GMT
#define VERION_1_0_0_0_MANDATORY_UPDATE_END		1558310750		// Monday, 20 May 2019 00:00:00 GMT
#define VERION_1_0_0_0_DEVELOPER_ADDRESS		"dSCXLHTZJJqTej8ZRszZxbLrS6dDGVJhw7"

/*
	Update 1.0.1.5:
	- Change to new developer address
*/
#define VERION_1_0_1_5_MANDATORY_UPDATE_START	1562094000		// Tuesday, 2 July 2019 19:00:00 GMT
#define VERION_1_0_1_5_MANDATORY_UPDATE_END		1562281200		// Thursday, 4 July 2019 23:00:00 GMT
#define VERION_1_0_1_5_DEVELOPER_ADDRESS		"dHy3LZvqX5B2rAAoLiA7Y7rpvkLXKTkD18"

/*
	Update 1.0.4.2:
	- Bittrex 1 billion payout
	- Because of security reasons we replaced the developer adress to a brand new one.
	- Patch security bug seesaw
*/
#define VERION_1_0_4_2_MANDATORY_UPDATE_BLOCK	403117
#define VERION_1_0_4_2_DEVELOPER_ADDRESS		"dafC1LknpDu7eALTf5DPcnPq2dwq7f9YPE"

/* Testnet developer address (v2.0.0.8 testnet bootstrap).
 * Block construction and validation on testnet always uses this regardless
 * of mainnet fork-date logic.  Mainnet logic still chooses between the
 * three mainnet developer addresses above based on block height/time. */
#define TESTNET_DEVELOPER_ADDRESS				"tRutwwW5LVYyYw72s3uTiWVGemNXh6FT5d"

/* Testnet reserve-phase cutoff.
 * On mainnet the reserve phase (80M XDN/block) ran from block 2 onwards
 * until money supply hit 8 billion -- a bootstrap mechanism for the legacy
 * cryptonote?standard codebase swap.  Testnet has no such legacy, so we
 * cap reserve phase to a small number of blocks early in chain history
 * (preserving the "first few blocks paid huge" pattern that mirrors
 * mainnet conceptually) and force all subsequent blocks to standard
 * 300 XDN regardless of supply. */
#define TESTNET_RESERVE_PHASE_END_HEIGHT			20

std::string getDevelopersAdress(const CBlockIndex* pindex);

#endif // FORK_H
