#ifndef INIT_H
#define INIT_H

#include <string>

class CWallet;

namespace boost {
	class thread_group;
} // namespace boost

extern CWallet* pwalletMain;
extern bool fOnlyTor;

// Set true at the very end of AppInit2, after wallet load and
// ReacceptWalletTransactions complete. Allows GUI poll callbacks to
// gate themselves out of running before the wallet is fully usable.
// Without this gate, polls that fire mid-load (e.g. the staking-icon
// QTimer) walk a partially-populated wallet and poison the balance
// caches with 0 for txes whose key is not yet in the keystore.
extern bool fWalletLoadComplete;

void StartShutdown();
bool ShutdownRequested();
void Shutdown();
bool AppInit2(boost::thread_group& threadGroup);
std::string HelpMessage();

#endif // INIT_H
