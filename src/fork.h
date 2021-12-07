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

std::string getDevelopersAdress(const CBlockIndex* pindex);

#endif // FORK_H
