// Copyright (c) 2012-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VERSION_H
#define BITCOIN_VERSION_H


/**
 * network protocol versioning
 */


//
// database format versioning
//
static const int DATABASE_VERSION = 70509;

//
// network protocol versioning
//
static const int PROTOCOL_VERSION = 62028;

// intial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 209;

// disconnect from peers older than this proto version
static const int MIN_PEER_PROTO_VERSION = 62017;

// minimum peer version accepted by MNenginePool
static const int MIN_POOL_PEER_PROTO_VERSION = 62017;
static const int MIN_INSTANTX_PROTO_VERSION = 62017;

//! minimum peer version that can receive masternode payments
// V1 - Last protocol version before update
// V2 - Newest protocol version
static const int MIN_MASTERNODE_PAYMENT_PROTO_VERSION_1 = 62017;
static const int MIN_MASTERNODE_PAYMENT_PROTO_VERSION_2 = 62017;

// nTime field added to CAddress, starting with this version;
// if possible, avoid requesting addresses nodes older than this
static const int CADDR_TIME_VERSION = 31402;

// only request blocks from nodes outside this range of versions
static const int NOBLKS_VERSION_START = 0;
static const int NOBLKS_VERSION_END = 62016;

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

// MasterNode peer IP basic relay system start (on and functional)
static const int64_t MIN_MASTERNODE_BSC_RELAY = 62026; // ON

#endif // BITCOIN_VERSION_H
