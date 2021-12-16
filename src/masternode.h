// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Darkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODE_H
#define MASTERNODE_H

class uint256;

#define MASTERNODE_NOT_PROCESSED               0 // initial state
#define MASTERNODE_IS_CAPABLE                  1
#define MASTERNODE_NOT_CAPABLE                 2
#define MASTERNODE_STOPPED                     3
#define MASTERNODE_INPUT_TOO_NEW               4
#define MASTERNODE_PORT_NOT_OPEN               6
#define MASTERNODE_PORT_OPEN                   7
#define MASTERNODE_SYNC_IN_PROCESS             8
#define MASTERNODE_REMOTELY_ENABLED            9

#define MASTERNODE_MIN_CONFIRMATIONS           7
#define MASTERNODE_MIN_DSEEP_SECONDS           (30*60)
#define MASTERNODE_MIN_DSEE_SECONDS            (5*60)
#define MASTERNODE_PING_SECONDS                (1*60)
#define MASTERNODE_EXPIRATION_SECONDS          (65*60)
#define MASTERNODE_REMOVAL_SECONDS             (70*60)

bool GetBlockHash(uint256& hash, int nBlockHeight);

#endif // MASTERNODE_H
