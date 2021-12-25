#ifndef CHAINPARAMS_H
#define CHAINPARAMS_H

#include "enums/cchainparams_network.h"

class CChainParams;

/**
 * Return the currently selected parameters. This won't change after app startup
 * outside of the unit tests.
 */
const CChainParams &Params();

/** Sets the params returned by Params() to those for the given network. */
void SelectParams(CChainParams_Network network);

/**
 * Looks for -regtest or -testnet and then calls SelectParams as appropriate.
 * Returns false if an invalid combination is given.
 */
bool SelectParamsFromCommandLine();

bool TestNet();

#endif // CHAINPARAMS_H
