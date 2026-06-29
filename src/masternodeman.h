#ifndef MASTERNODEMAN_H
#define MASTERNODEMAN_H

#define MASTERNODES_DUMP_SECONDS	(15*60)
#define MASTERNODES_DSEG_SECONDS	(3*60*60)

// v2.0.0.8: short retry interval used by DsegUpdate when the node still
// has an empty (or near-empty) masternode list -- i.e. the previous dseg
// request evidently did not deliver a usable list (lost, peer dropped,
// partial). The full MASTERNODES_DSEG_SECONDS interval is used only once
// the list is actually populated, so a node that synced cleanly does not
// re-ask, while a node that got nothing recovers in minutes not hours.
#define MASTERNODES_DSEG_RETRY_SECONDS	(3*60)

void DumpMasternodes();

#endif // MASTERNODEMAN_H
