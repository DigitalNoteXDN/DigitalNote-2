#ifndef VERSION_H
#define VERSION_H

#include "clientversion.h"
#include <stdint.h>
#include <string>

//
// client versioning
//

static const int CLIENT_VERSION = 1000000 * CLIENT_VERSION_MAJOR
								+ 10000 * CLIENT_VERSION_MINOR
								+ 100 * CLIENT_VERSION_REVISION
								+ 1 * CLIENT_VERSION_BUILD;

extern const std::string CLIENT_NAME;
extern const std::string CLIENT_BUILD;
extern const std::string CLIENT_DATE;

//
// database format versioning
//
static const int DATABASE_VERSION = 70509;

//
// network protocol versioning
//
// Lineage (62057 and 62058 are BOTH v2.0.0.8 -- distinguished pre- vs
// post-queue so nodes can tell M1Q-capable peers from the earlier build):
//   62055 = v2.0.0.7
//   62057 = v2.0.0.8 PRE-QUEUE  (per-height single vote -- the earlier
//           testnet build; this protocol is now superseded/dead)
//   62058 = v2.0.0.8 POST-QUEUE (this build: M1Q queue-based voting).
//           Adds the mnvotequeue / getmnqueues messages; it is the version
//           at which a node is counted as a queue-voting peer.
//
// MIN_PEER_PROTO_VERSION is intentionally NOT bumped -- v2.0.0.8 nodes
// continue to accept v2.0.0.7 peers (62055) for the entire deployment
// window, and v2.0.0.7 peers continue to accept v2.0.0.8 nodes because
// 62056 > 62052.  Soft-fork compatible.
static const int PROTOCOL_VERSION = 62058;

// intial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 209;

// disconnect from peers older than this proto version
static const int MIN_PEER_PROTO_VERSION = 62052;

// minimum peer version accepted by MNenginePool
static const int MIN_POOL_PEER_PROTO_VERSION = 62050;
static const int MIN_INSTANTX_PROTO_VERSION = 62050;

//! minimum peer version that can receive masternode payments
// V1 - Last protocol version before update
// V2 - Newest protocol version
static const int MIN_MASTERNODE_PAYMENT_PROTO_VERSION_1 = 62051;
static const int MIN_MASTERNODE_PAYMENT_PROTO_VERSION_2 = 62051;

// nTime field added to CAddress, starting with this version;
// if possible, avoid requesting addresses nodes older than this
static const int CADDR_TIME_VERSION = 31402;

// only request blocks from nodes outside this range of versions
static const int NOBLKS_VERSION_START = 0;
static const int NOBLKS_VERSION_END = 62051;

// hard cutoff time for legacy network connections
static const int64_t HRD_LEGACY_CUTOFF = 9993058800; // OFF (NOT TOGGLED)

// hard cutoff time for future network connections
static const int64_t HRD_FUTURE_CUTOFF = 9993058800; // OFF (NOT TOGGLED)

// BIP 0031, pong message, is enabled for all versions AFTER this one
static const int BIP0031_VERSION = 60000;

// "mempool" command, enhanced "getdata" behavior starts with this version:
static const int MEMPOOL_GD_VERSION = 60002;

// MasterNode peer IP advanced relay system start (Unfinished, not used)
static const int64_t MIN_MASTERNODE_ADV_RELAY = 9993058800; // OFF (NOT TOGGLED)

#endif
