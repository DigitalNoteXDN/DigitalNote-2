#ifndef CMNQUEUESNAPSHOT_H
#define CMNQUEUESNAPSHOT_H

#include "ctxin.h"
#include "cscript.h"

/**
 * CMnPaymentSnapshotEntry -- one masternode's chain-derived payment state,
 * captured under mnodeman.cs in a single lock acquisition (v2.0.0.8 M1Q
 * spec S18.1).  Consumed by the queue forward-simulation in
 * CActiveMasternode::BroadcastQueue.
 *
 * This is MN-manager-domain data (identity + collateral confirm height +
 * last-paid height), not queue-specific, so it lives alongside the manager
 * rather than the queue voting code.  The queue simulation is just one
 * consumer.
 */
struct CMnPaymentSnapshotEntry
{
	COutPoint vin;               // identity + deterministic tiebreak key
	CScript   payeeScript;       // payout script (GetScriptForDestination of pubkey)
	int       confirmedHeight;   // GetCollateralConfirmedHeight() (< 0 if unresolved)
	bool      hasPaid;           // true if mapLastPaidHeight had an entry
	int       paidHeight;        // mapLastPaidHeight value (valid iff hasPaid)

	CMnPaymentSnapshotEntry()
		: confirmedHeight(-1), hasPaid(false), paidHeight(0)
	{
	}
};

#endif // CMNQUEUESNAPSHOT_H
