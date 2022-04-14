#ifndef CMNENGINEBROADCASTTX_H
#define CMNENGINEBROADCASTTX_H

#include <vector>

#include "ctransaction.h"
#include "ctxin.h"

/** Helper class to store MNengine transaction (tx) information.
 */
class CMNengineBroadcastTx
{
public:
	CTransaction tx;
	CTxIn vin;
	std::vector<unsigned char> vchSig;
	int64_t sigTime;
};

#endif // CMNENGINEBROADCASTTX_H
