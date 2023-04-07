#ifndef CMAINSIGNALS_H
#define CMAINSIGNALS_H

#include <boost/signals2/signal.hpp>

class CTransaction;
class CBlock;
class uint256;
class CBlockLocator;

struct CMainSignals
{
	// Notifies listeners of updated transaction data (passing hash, transaction, and optionally the block it is found in.
	boost::signals2::signal<void (const CTransaction&, const CBlock*, bool, bool)> SyncTransaction;
	// Notifies listeners of an erased transaction (currently disabled, requires transaction replacement).
	boost::signals2::signal<void (const uint256 &)> EraseTransaction;
	// Notifies listeners of an updated transaction without new data (for now: a coinbase potentially becoming visible).
	boost::signals2::signal<void (const uint256 &)> UpdatedTransaction;
	// Notifies listeners of a new active block chain.
	boost::signals2::signal<void (const CBlockLocator &)> SetBestChain;
	// Notifies listeners about an inventory item being seen on the network.
	boost::signals2::signal<void (const uint256 &)> Inventory;
	// Tells listeners to broadcast their data.
	boost::signals2::signal<void (bool)> Broadcast;
};

#endif // CMAINSIGNALS_H
