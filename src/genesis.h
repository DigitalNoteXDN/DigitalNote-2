// Copyright (c) 2016-2020 The CryptoCoderz Team / Espers
// Copyright (c) 2018-2020 The CryptoCoderz Team / INSaNe project
// Copyright (c) 2018-2020 The Rubix project
// Copyright (c) 2018-2020 The DigitalNote project
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_GENESIS_H
#define BITCOIN_GENESIS_H

/** Genesis Start Time */
static const unsigned int timeGenesisBlock = 1547848800; // Friday, January 18, 2019 10:00:00 PM
/** Genesis TestNet Start Time */
static const unsigned int timeTestNetGenesis = 1547848800+30; // Friday, January 18, 2019 10:00:30 PM
/** Genesis RegNet Start Time */
static const unsigned int timeRegNetGenesis = 1547848800+90; // Friday, January 18, 2019 10:01:30 PM
/** Genesis Nonce Mainnet*/
static const unsigned int nNonceMain = 0;
/** Genesis Nonce Testnet */
static const unsigned int nNonceTest = 0;
/** Genesis Nonce Regnet */
static const unsigned int nNonceReg = 8;
/** Main Net Genesis Block */
static const uint256 nGenesisBlock("0x");
/** Test Net Genesis Block */
static const uint256 hashTestNetGenesisBlock("0x");
/** Reg Net Genesis Block */
static const uint256 hashRegNetGenesisBlock("0x");
/** Genesis Merkleroot */
static const uint256 nGenesisMerkle("0x");

#endif // BITCOIN_GENESIS_H
