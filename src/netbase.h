#ifndef NETBASE_H
#define NETBASE_H

#include <string>
#include <vector>

#include "net/network.h"
#include "net/proxytype.h"

class CService;
class CNetAddr;

extern int nConnectTimeout;
extern bool fNameLookup;

enum Network ParseNetwork(std::string net);
std::string GetNetworkName(enum Network net);
void SplitHostPort(const std::string &in, int &portOut, std::string &hostOut);
bool SetProxy(enum Network net, CService addrProxy, int nSocksVersion = 5);
bool GetProxy(enum Network net, proxyType &proxyInfoOut);
bool IsProxy(const CNetAddr &addr);
bool SetNameProxy(CService addrProxy, int nSocksVersion = 5);
bool HaveNameProxy();
bool LookupHost(const char *pszName, std::vector<CNetAddr>& vIP, unsigned int nMaxSolutions = 0, bool fAllowLookup = true);
bool LookupHostNumeric(const char *pszName, std::vector<CNetAddr>& vIP, unsigned int nMaxSolutions = 0);
bool Lookup(const char *pszName, CService& addr, int portDefault = 0, bool fAllowLookup = true);
bool Lookup(const char *pszName, std::vector<CService>& vAddr, int portDefault = 0, bool fAllowLookup = true, unsigned int nMaxSolutions = 0);
bool LookupNumeric(const char *pszName, CService& addr, int portDefault = 0);
bool ConnectSocket(const CService &addr, SOCKET& hSocketRet, int nTimeout = nConnectTimeout);
bool ConnectSocketByName(CService &addr, SOCKET& hSocketRet, const char *pszDest, int portDefault = 0, int nTimeout = nConnectTimeout);
/** Return readable error string for a network error code */
std::string NetworkErrorString(int err);

#endif // NETBASE_H
