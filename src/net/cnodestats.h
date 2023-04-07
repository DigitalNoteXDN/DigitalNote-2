#ifndef CNODESTATS_H
#define CNODESTATS_H

#include <cstdint>
#include <string>

#include "net/cnode.h"

class CNodeStats
{
public:
	NodeId nodeid;
	uint64_t nServices;
	int64_t nLastSend;
	int64_t nLastRecv;
	int64_t nTimeConnected;
	int64_t nTimeOffset;
	std::string addrName;
	int nVersion;
	std::string cleanSubVer;
	std::string strSubVer;
	bool fInbound;
	int nStartingHeight;
	uint64_t nSendBytes;
	uint64_t nRecvBytes;
	bool fSyncNode;
	double dPingTime;
	double dPingWait;
	std::string addrLocal;
};

#endif // CNODESTATS_H
