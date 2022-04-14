#ifndef CWALLETINTERFACE_H
#define CWALLETINTERFACE_H

#include "main.h"

class CTransaction;
class CBlock;
class uint256;
class CBlockLocator;
class CWalletInterface;

class CWalletInterface
{
protected:
	virtual void SyncTransaction(const CTransaction &tx, const CBlock *pblock, bool fConnect, bool fFixSpentCoins) =0;
	virtual void EraseFromWallet(const uint256 &hash) =0;
	virtual void SetBestChain(const CBlockLocator &locator) =0;
	virtual bool UpdatedTransaction(const uint256 &hash) =0;
	virtual void Inventory(const uint256 &hash) =0;
	virtual void ResendWalletTransactions(bool fForce) =0;
	friend void ::RegisterWallet(CWalletInterface*);
	friend void ::UnregisterWallet(CWalletInterface*);
	friend void ::UnregisterAllWallets();
};

#endif // CWALLETINTERFACE_H
