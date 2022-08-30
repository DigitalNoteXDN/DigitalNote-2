#include "compat.h"

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/predicate.hpp> // for startswith() and endswith()

#ifndef WIN32
#include <fcntl.h>
#endif

#include "util.h"
#include "thread.h"
#include "net/cservice.h"
#include "net/myclosesocket.h"

#include "netbase.h"

// Settings
static proxyType proxyInfo[NET_MAX];
static proxyType nameproxyInfo;
static CCriticalSection cs_proxyInfos;
int nConnectTimeout = 5000;
bool fNameLookup = false;

enum Network ParseNetwork(std::string net)
{
	boost::to_lower(net);

	if (net == "ipv4")
	{
		return NET_IPV4;
	}

	if (net == "ipv6")
	{
		return NET_IPV6;
	}

	if (net == "tor")
	{
		return NET_TOR;
	}

	if (net == "i2p")
	{
		return NET_I2P;
	}

	return NET_UNROUTABLE;
}

std::string GetNetworkName(enum Network net)
{
    switch (net)
	{
		case NET_IPV4:
			return "ipv4";
		case NET_IPV6:
			return "ipv6";
		case NET_TOR:
			return "onion";
		default:
			return "";
    }
}

void SplitHostPort(const std::string &in, int &portOut, std::string &hostOut)
{
	std::string _in = in;

	size_t colon = _in.find_last_of(':');
	// if a : is found, and it either follows a [...], or no other : is _in the string, treat it as port separator
	bool fHaveColon = colon != _in.npos;
	// if there is a colon, and _in[0]=='[', colon is not 0, so _in[colon-1] is safe
	bool fBracketed = fHaveColon && (_in[0]=='[' && _in[colon-1]==']');
	bool fMultiColon = fHaveColon && (_in.find_last_of(':',colon-1) != _in.npos);

	if (fHaveColon && (colon==0 || fBracketed || !fMultiColon))
	{
		char *endp = NULL;
		int n = strtol(_in.c_str() + colon + 1, &endp, 10);
		
		if (endp && *endp == 0 && n >= 0)
		{
			_in = _in.substr(0, colon);
			
			if (n > 0 && n < 0x10000)
			{
				portOut = n;
			}
		}
	}

	if (_in.size()>0 && _in[0] == '[' && _in[_in.size()-1] == ']')
	{
		hostOut = _in.substr(1, _in.size()-2);
	}
	else
	{
		hostOut = _in;
	}
}

bool static LookupIntern(const char *pszName, std::vector<CNetAddr>& vIP, unsigned int nMaxSolutions, bool fAllowLookup)
{
	vIP.clear();

	{
		CNetAddr addr;
		
		if (addr.SetSpecial(std::string(pszName)))
		{
			vIP.push_back(addr);
			
			return true;
		}
	}

	struct addrinfo aiHint;
	memset(&aiHint, 0, sizeof(struct addrinfo));

	aiHint.ai_socktype = SOCK_STREAM;
	aiHint.ai_protocol = IPPROTO_TCP;
	aiHint.ai_family = AF_UNSPEC;
#ifdef WIN32
	aiHint.ai_flags = fAllowLookup ? 0 : AI_NUMERICHOST;
#else
	aiHint.ai_flags = fAllowLookup ? AI_ADDRCONFIG : AI_NUMERICHOST;
#endif
	struct addrinfo *aiRes = NULL;
	int nErr = getaddrinfo(pszName, NULL, &aiHint, &aiRes);

	if (nErr)
	{
		return false;
	}

	struct addrinfo *aiTrav = aiRes;

	while (aiTrav != NULL && (nMaxSolutions == 0 || vIP.size() < nMaxSolutions))
	{
		if (aiTrav->ai_family == AF_INET)
		{
			assert(aiTrav->ai_addrlen >= sizeof(sockaddr_in));
			vIP.push_back(CNetAddr(((struct sockaddr_in*)(aiTrav->ai_addr))->sin_addr));
		}

		if (aiTrav->ai_family == AF_INET6)
		{
			assert(aiTrav->ai_addrlen >= sizeof(sockaddr_in6));
			vIP.push_back(CNetAddr(((struct sockaddr_in6*)(aiTrav->ai_addr))->sin6_addr));
		}

		aiTrav = aiTrav->ai_next;
	}

	freeaddrinfo(aiRes);

	return (vIP.size() > 0);
}

bool LookupHost(const char *pszName, std::vector<CNetAddr>& vIP, unsigned int nMaxSolutions, bool fAllowLookup)
{
	std::string strHost(pszName);

	if (strHost.empty())
	{
		return false;
	}

	if (boost::algorithm::starts_with(strHost, "[") && boost::algorithm::ends_with(strHost, "]"))
	{
		strHost = strHost.substr(1, strHost.size() - 2);
	}

	return LookupIntern(strHost.c_str(), vIP, nMaxSolutions, fAllowLookup);
}

bool LookupHostNumeric(const char *pszName, std::vector<CNetAddr>& vIP, unsigned int nMaxSolutions)
{
	return LookupHost(pszName, vIP, nMaxSolutions, false);
}

bool Lookup(const char *pszName, std::vector<CService>& vAddr, int portDefault, bool fAllowLookup, unsigned int nMaxSolutions)
{
	if (pszName[0] == 0)
	{
		return false;
	}

	int port = portDefault;
	std::string hostname = "";
	SplitHostPort(std::string(pszName), port, hostname);

	std::vector<CNetAddr> vIP;
	bool fRet = LookupIntern(hostname.c_str(), vIP, nMaxSolutions, fAllowLookup);

	if (!fRet)
	{
		return false;
	}

	vAddr.resize(vIP.size());

	for (unsigned int i = 0; i < vIP.size(); i++)
	{
		vAddr[i] = CService(vIP[i], port);
	}

	return true;
}

bool Lookup(const char *pszName, CService& addr, int portDefault, bool fAllowLookup)
{
	std::vector<CService> vService;
	bool fRet = Lookup(pszName, vService, portDefault, fAllowLookup, 1);
	
	if (!fRet)
	{
		return false;
	}
	
	addr = vService[0];
	
	return true;
}

bool LookupNumeric(const char *pszName, CService& addr, int portDefault)
{
	return Lookup(pszName, addr, portDefault, false);
}

bool static Socks4(const CService &addrDest, SOCKET& hSocket)
{
	LogPrintf("SOCKS4 connecting %s\n", addrDest.ToString());

	if (!addrDest.IsIPv4())
	{
		closesocket(hSocket);
		
		return error("Proxy destination is not IPv4");
	}

	char pszSocks4IP[] = "\4\1\0\0\0\0\0\0user";
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);

	if (!addrDest.GetSockAddr((struct sockaddr*)&addr, &len) || addr.sin_family != AF_INET)
	{
		closesocket(hSocket);
		
		return error("Cannot get proxy destination address");
	}

	memcpy(pszSocks4IP + 2, &addr.sin_port, 2);
	memcpy(pszSocks4IP + 4, &addr.sin_addr, 4);

	char* pszSocks4 = pszSocks4IP;
	int nSize = sizeof(pszSocks4IP);

	int ret = send(hSocket, pszSocks4, nSize, MSG_NOSIGNAL);

	if (ret != nSize)
	{
		closesocket(hSocket);
		
		return error("Error sending to proxy");
	}

	char pchRet[8];

	if (recv(hSocket, pchRet, 8, 0) != 8)
	{
		closesocket(hSocket);
		
		return error("Error reading proxy response");
	}

	if (pchRet[1] != 0x5a)
	{
		closesocket(hSocket);
		
		if (pchRet[1] != 0x5b)
		{
			LogPrintf("ERROR: Proxy returned error %d\n", pchRet[1]);
		}
		
		return false;
	}

	LogPrintf("SOCKS4 connected %s\n", addrDest.ToString());

	return true;
}

bool static Socks5(std::string strDest, int port, SOCKET& hSocket)
{
	LogPrintf("SOCKS5 connecting %s\n", strDest);

	if (strDest.size() > 255)
	{
		closesocket(hSocket);
		
		return error("Hostname too long");
	}

	char pszSocks5Init[] = "\5\1\0";
	ssize_t nSize = sizeof(pszSocks5Init) - 1;

	ssize_t ret = send(hSocket, pszSocks5Init, nSize, MSG_NOSIGNAL);

	if (ret != nSize)
	{
		closesocket(hSocket);
		
		return error("Error sending to proxy");
	}

	char pchRet1[2];

	if (recv(hSocket, pchRet1, 2, 0) != 2)
	{
		closesocket(hSocket);
		
		return error("Error reading proxy response");
	}

	if (pchRet1[0] != 0x05 || pchRet1[1] != 0x00)
	{
		closesocket(hSocket);
		
		return error("Proxy failed to initialize");
	}

	std::string strSocks5("\5\1");

	strSocks5 += '\000'; strSocks5 += '\003';
	strSocks5 += static_cast<char>(std::min((int)strDest.size(), 255));
	strSocks5 += strDest;
	strSocks5 += static_cast<char>((port >> 8) & 0xFF);
	strSocks5 += static_cast<char>((port >> 0) & 0xFF);

	ret = send(hSocket, strSocks5.c_str(), strSocks5.size(), MSG_NOSIGNAL);

	if (ret != (ssize_t)strSocks5.size())
	{
		closesocket(hSocket);
		
		return error("Error sending to proxy");
	}

	char pchRet2[4];

	if (recv(hSocket, pchRet2, 4, 0) != 4)
	{
		closesocket(hSocket);
		
		return error("Error reading proxy response");
	}

	if (pchRet2[0] != 0x05)
	{
		closesocket(hSocket);
		
		return error("Proxy failed to accept request");
	}

	if (pchRet2[1] != 0x00)
	{
		closesocket(hSocket);
		
		switch (pchRet2[1])
		{
			case 0x01: return error("Proxy error: general failure");
			case 0x02: return error("Proxy error: connection not allowed");
			case 0x03: return error("Proxy error: network unreachable");
			case 0x04: return error("Proxy error: host unreachable");
			case 0x05: return error("Proxy error: connection refused");
			case 0x06: return error("Proxy error: TTL expired");
			case 0x07: return error("Proxy error: protocol error");
			case 0x08: return error("Proxy error: address type not supported");
			default:   return error("Proxy error: unknown");
		}
	}

	if (pchRet2[2] != 0x00)
	{
		closesocket(hSocket);
		
		return error("Error: malformed proxy response");
	}

	char pchRet3[256];

	switch (pchRet2[3])
	{
		case 0x01:
			ret = recv(hSocket, pchRet3, 4, 0) != 4;
			break;
		
		case 0x04:
			ret = recv(hSocket, pchRet3, 16, 0) != 16;
			break;
		
		case 0x03:
		{
			ret = recv(hSocket, pchRet3, 1, 0) != 1;
			
			if (ret)
			{
				closesocket(hSocket);
				
				return error("Error reading from proxy");
			}
			
			int nRecv = pchRet3[0];
			
			ret = recv(hSocket, pchRet3, nRecv, 0) != nRecv;
			
			break;
		}
		
		default:
			closesocket(hSocket);
			return error("Error: malformed proxy response");
	}

	if (ret)
	{
		closesocket(hSocket);
		
		return error("Error reading from proxy");
	}

	if (recv(hSocket, pchRet3, 2, 0) != 2)
	{
		closesocket(hSocket);
		
		return error("Error reading from proxy");
	}

	LogPrintf("SOCKS5 connected %s\n", strDest);

	return true;
}

bool static ConnectSocketDirectly(const CService &addrConnect, SOCKET& hSocketRet, int nTimeout)
{
	hSocketRet = INVALID_SOCKET;

	struct sockaddr_storage sockaddr;
	socklen_t len = sizeof(sockaddr);

	if (!addrConnect.GetSockAddr((struct sockaddr*)&sockaddr, &len))
	{
		LogPrintf("Cannot connect to %s: unsupported network\n", addrConnect.ToString());
		
		return false;
	}

	SOCKET hSocket = socket(((struct sockaddr*)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);

	if (hSocket == INVALID_SOCKET)
	{
		return false;
	}

	int set = 1;
#ifdef SO_NOSIGPIPE
	// Different way of disabling SIGPIPE on BSD
	setsockopt(hSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
#endif

	//Disable Nagle's algorithm
#ifdef WIN32
	setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&set, sizeof(int));
#else
	setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (void*)&set, sizeof(int));
#endif

#ifdef WIN32
	u_long fNonblock = 1;
	
	if (ioctlsocket(hSocket, FIONBIO, &fNonblock) == SOCKET_ERROR)
#else
	int fFlags = fcntl(hSocket, F_GETFL, 0);
	
	if (fcntl(hSocket, F_SETFL, fFlags | O_NONBLOCK) == -1)
#endif
	{
		closesocket(hSocket);
		
		return false;
	}

	if (connect(hSocket, (struct sockaddr*)&sockaddr, len) == SOCKET_ERROR)
	{
		// WSAEINVAL is here because some legacy version of winsock uses it
		if (WSAGetLastError() == WSAEINPROGRESS || WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINVAL)
		{
			struct timeval timeout;
			timeout.tv_sec  = nTimeout / 1000;
			timeout.tv_usec = (nTimeout % 1000) * 1000;

			fd_set fdset;
			
			FD_ZERO(&fdset);
			FD_SET(hSocket, &fdset);
			
			int nRet = select(hSocket + 1, NULL, &fdset, NULL, &timeout);
			
			if (nRet == 0)
			{
				LogPrint("net", "connection to %s timeout\n", addrConnect.ToString());
				
				closesocket(hSocket);
				
				return false;
			}
			
			if (nRet == SOCKET_ERROR)
			{
				LogPrintf("select() for %s failed: %i\n", addrConnect.ToString(), WSAGetLastError());
				
				closesocket(hSocket);
				
				return false;
			}
			
			socklen_t nRetSize = sizeof(nRet);
#ifdef WIN32
			if (getsockopt(hSocket, SOL_SOCKET, SO_ERROR, (char*)(&nRet), &nRetSize) == SOCKET_ERROR)
#else
			if (getsockopt(hSocket, SOL_SOCKET, SO_ERROR, &nRet, &nRetSize) == SOCKET_ERROR)
#endif
			{
				LogPrintf("getsockopt() for %s failed: %i\n", addrConnect.ToString(), WSAGetLastError());
				
				closesocket(hSocket);
				
				return false;
			}
			
			if (nRet != 0)
			{
				LogPrintf("connect() to %s failed after select(): %s\n", addrConnect.ToString(), strerror(nRet));
				
				closesocket(hSocket);
				
				return false;
			}
		}
#ifdef WIN32
		else if (WSAGetLastError() != WSAEISCONN)
#else
		else
#endif
		{
			LogPrintf("connect() to %s failed: %i\n", addrConnect.ToString(), WSAGetLastError());
			
			closesocket(hSocket);
			
			return false;
		}
	}

	// this isn't even strictly necessary
	// CNode::ConnectNode immediately turns the socket back to non-blocking
	// but we'll turn it back to blocking just in case
#ifdef WIN32
	fNonblock = 0;
	
	if (ioctlsocket(hSocket, FIONBIO, &fNonblock) == SOCKET_ERROR)
#else
	fFlags = fcntl(hSocket, F_GETFL, 0);

	if (fcntl(hSocket, F_SETFL, fFlags & ~O_NONBLOCK) == SOCKET_ERROR)
#endif
	{
		closesocket(hSocket);
		
		return false;
	}

	hSocketRet = hSocket;
	
	return true;
}

bool SetProxy(enum Network net, CService addrProxy, int nSocksVersion)
{
	assert(net >= 0 && net < NET_MAX);

	if (nSocksVersion != 0 && nSocksVersion != 4 && nSocksVersion != 5)
	{
		return false;
	}

	if (nSocksVersion != 0 && !addrProxy.IsValid())
	{
		return false;
	}

	LOCK(cs_proxyInfos);

	proxyInfo[net] = std::make_pair(addrProxy, nSocksVersion);

	return true;
}

bool GetProxy(enum Network net, proxyType &proxyInfoOut)
{
	assert(net >= 0 && net < NET_MAX);

	LOCK(cs_proxyInfos);

	if (!proxyInfo[net].second)
	{
		return false;
	}

	proxyInfoOut = proxyInfo[net];

	return true;
}

bool SetNameProxy(CService addrProxy, int nSocksVersion)
{
	if (nSocksVersion != 0 && nSocksVersion != 5)
	{
		return false;
	}

	if (nSocksVersion != 0 && !addrProxy.IsValid())
	{
		return false;
	}

	LOCK(cs_proxyInfos);

	nameproxyInfo = std::make_pair(addrProxy, nSocksVersion);

	return true;
}

bool GetNameProxy(proxyType &nameproxyInfoOut)
{
	LOCK(cs_proxyInfos);

	if (!nameproxyInfo.second)
	{
		return false;
	}

	nameproxyInfoOut = nameproxyInfo;

	return true;
}

bool HaveNameProxy()
{
	LOCK(cs_proxyInfos);

	return nameproxyInfo.second != 0;
}

bool IsProxy(const CNetAddr &addr)
{
	LOCK(cs_proxyInfos);

	for (int i = 0; i < NET_MAX; i++)
	{	
		if (proxyInfo[i].second && (addr == (CNetAddr)proxyInfo[i].first))
		{
			return true;
		}
	}

	return false;
}

bool ConnectSocket(const CService &addrDest, SOCKET& hSocketRet, int nTimeout)
{
	proxyType proxy;

	// no proxy needed
	if (!GetProxy(addrDest.GetNetwork(), proxy))
	{
		return ConnectSocketDirectly(addrDest, hSocketRet, nTimeout);
	}

	SOCKET hSocket = INVALID_SOCKET;

	// first connect to proxy server
	if (!ConnectSocketDirectly(proxy.first, hSocket, nTimeout))
	{
		return false;
	}

	// do socks negotiation
	switch (proxy.second)
	{
		case 4:
			if (!Socks4(addrDest, hSocket))
			{
				return false;
			}
			
			break;
		
		case 5:
			if (!Socks5(addrDest.ToStringIP(), addrDest.GetPort(), hSocket))
			{
				return false;
			}
			
			break;
		
		default:
			closesocket(hSocket);
			return false;
	}

	hSocketRet = hSocket;
	
	return true;
}

bool ConnectSocketByName(CService &addr, SOCKET& hSocketRet, const char *pszDest, int portDefault, int nTimeout)
{
	std::string strDest;
	int port = portDefault;
	SplitHostPort(std::string(pszDest), port, strDest);

	SOCKET hSocket = INVALID_SOCKET;

	proxyType nameproxy;
	GetNameProxy(nameproxy);

	CService addrResolved(CNetAddr(strDest, fNameLookup && !nameproxy.second), port);

	if (addrResolved.IsValid())
	{
		addr = addrResolved;
		
		return ConnectSocket(addr, hSocketRet, nTimeout);
	}

	addr = CService("0.0.0.0:0");

	if (!nameproxy.second)
	{
		return false;
	}

	if (!ConnectSocketDirectly(nameproxy.first, hSocket, nTimeout))
	{
		return false;
	}

	switch(nameproxy.second)
	{
		default:
		case 4:
			closesocket(hSocket);
			
			return false;
		
		case 5:
			if (!Socks5(strDest, port, hSocket))
			{
				return false;
			}
			
			break;
	}

	hSocketRet = hSocket;

	return true;
}

#ifdef WIN32
std::string NetworkErrorString(int err)
{
	char buf[256];

	buf[0] = 0;

	if(
		FormatMessageA(
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
			NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof(buf), NULL
		)
	)
	{
		return strprintf("%s (%d)", buf, err);
	}
	else
	{
		return strprintf("Unknown error (%d)", err);
	}
}
#else // WIN32
std::string NetworkErrorString(int err)
{
	char buf[256];
	const char *s = buf;
	buf[0] = 0;

	/* Too bad there are two incompatible implementations of the
	 * thread-safe strerror. */
	 
#ifdef STRERROR_R_CHAR_P /* GNU variant can return a pointer outside the passed buffer */
	s = strerror_r(err, buf, sizeof(buf));
#else
	/* POSIX variant always returns message in buffer */
	(void) strerror_r(err, buf, sizeof(buf));
#endif

	return strprintf("%s (%d)", s, err);
}
#endif // WIN32
