#ifndef NET_CSERVICE_H
#define NET_CSERVICE_H

#include "compat.h"

#include <vector>
#include <string>

#include "net/cnetaddr.h"

#ifdef WIN32
	// In MSVC, this is defined as a macro, undefine it to prevent a compile and link error
	#undef SetPort
#endif

class CService;

bool operator==(const CService& a, const CService& b);
bool operator!=(const CService& a, const CService& b);
bool operator<(const CService& a, const CService& b);

/** A combination of a network address (CNetAddr) and a (TCP) port */
class CService : public CNetAddr
{
protected:
	unsigned short port; // host order

public:
	CService();
	CService(const CNetAddr& ip, unsigned short port);
	CService(const struct in_addr& ipv4Addr, unsigned short port);
	CService(const struct sockaddr_in& addr);
	CService(const struct in6_addr& ipv6Addr, unsigned short port);
	CService(const struct sockaddr_in6& addr);
	explicit CService(const char *pszIpPort, int portDefault, bool fAllowLookup = false);
	explicit CService(const char *pszIpPort, bool fAllowLookup = false);
	explicit CService(const std::string& strIpPort, int portDefault, bool fAllowLookup = false);
	explicit CService(const std::string& strIpPort, bool fAllowLookup = false);
	
	void Init();
	void SetPort(unsigned short portIn);
	unsigned short GetPort() const;
	bool GetSockAddr(struct sockaddr* paddr, socklen_t *addrlen) const;
	bool SetSockAddr(const struct sockaddr* paddr);
	std::vector<unsigned char> GetKey() const;
	std::string ToString() const;
	std::string ToStringPort() const;
	std::string ToStringIPPort() const;
	void print() const;
	
	friend bool operator==(const CService& a, const CService& b);
	friend bool operator!=(const CService& a, const CService& b);
	friend bool operator<(const CService& a, const CService& b);
	
	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // NET_CSERVICE_H
