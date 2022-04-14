#ifndef INIT_H
#define INIT_H

#include <string>

class CWallet;

namespace boost {
	class thread_group;
} // namespace boost

extern CWallet* pwalletMain;
extern bool fOnlyTor;

void StartShutdown();
bool ShutdownRequested();
void Shutdown();
bool AppInit2(boost::thread_group& threadGroup);
std::string HelpMessage();

#endif // INIT_H
