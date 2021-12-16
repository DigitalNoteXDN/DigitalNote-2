// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "compat.h"

#ifdef WIN32
#include <string.h>
#else
#include <fcntl.h>
#endif

#ifdef USE_UPNP
#include <miniupnpc-2.1/miniupnpc.h>
#include <miniupnpc-2.1/miniwget.h>
#include <miniupnpc-2.1/upnpcommands.h>
#include <miniupnpc-2.1/upnperrors.h>
#endif

#include <boost/thread.hpp>
#include <boost/filesystem.hpp>

#include "cinv.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "net/cnode.h"
#include "net/cbanentry.h"
#include "net/cnodestats.h"
#include "net/caddrdb.h"
#include "net/cbandb.h"
#include "net/cnetcleanup.h"
#include "net/cnetmessage.h"
#include "ctxmempool.h"
#include "cdnsseeddata.h"
#include "protocol.h"
#include "caddrman.h"
#include "caddrinfo.h"
#include "net/have_msg_nosignal.h"
#include "net/csubnet.h"
#include "netbase.h"
#include "net/myclosesocket.h"
#include "main_extern.h"
#include "ctransaction.h"
#include "init.h"
#include "thread.h"
#include "ui_interface.h"
#include "ui_translate.h"
#include "enums/serialize_type.h"
#include "types/cserializedata.h"

#include "net.h"

// Dump addresses to peers.dat every 15 minutes (900s)
#define DUMP_ADDRESSES_INTERVAL 900

static const int MAX_OUTBOUND_CONNECTIONS = 12;

//
// Global state variables
//
bool fDiscover = true;
uint64_t nLocalServices = NODE_NETWORK;
CCriticalSection cs_mapLocalHost;
std::map<CNetAddr, LocalServiceInfo> mapLocalHost;
static bool vfReachable[NET_MAX] = {};
static bool vfLimited[NET_MAX] = {};
CNode* pnodeLocalHost = NULL;
CNode* pnodeSync = NULL;
uint64_t nLocalHostNonce = 0;
std::vector<SOCKET> vhListenSocket;
CAddrMan addrman;
std::string strSubVersion;
int nMaxConnections = GetArg("-maxconnections", 125);

std::vector<CNode*> vNodes;
CCriticalSection cs_vNodes;
std::map<CInv, CDataStream> mapRelay;
std::deque<std::pair<int64_t, CInv> > vRelayExpiration;
CCriticalSection cs_mapRelay;
limitedmap<CInv, int64_t> mapAlreadyAskedFor(MAX_INV_SZ);

static std::deque<std::string> vOneShots;
CCriticalSection cs_vOneShots;

std::set<CNetAddr> setservAddNodeAddresses;
CCriticalSection cs_setservAddNodeAddresses;

std::vector<std::string> vAddedNodes;
CCriticalSection cs_vAddedNodes;

NodeId nLastNodeId = 0;
CCriticalSection cs_nLastNodeId;

CSemaphore *semOutbound = NULL;

// Signals for message handling
static CNodeSignals g_net_signals;

std::list<CNode*> vNodesDisconnected;
int LastRefreshstamp = 0;
int RefreshesDone = 0;
bool FirstCycle = true;
static CNetCleanup instance_of_cnetcleanup;

CNodeSignals& GetNodeSignals()
{
	return g_net_signals;
}

unsigned int ReceiveFloodSize()
{
	return 1000 * GetArg("-maxreceivebuffer", 5 * 1000);
}

unsigned int SendBufferSize()
{
	return 1000 * GetArg("-maxsendbuffer", 1 * 1000);
}

void AddOneShot(std::string strDest)
{
	LOCK(cs_vOneShots);

	vOneShots.push_back(strDest);
}

unsigned short GetListenPort()
{
	return (unsigned short)(GetArg("-port", Params().GetDefaultPort()));
}

// find 'best' local address for a particular peer
bool GetLocal(CService& addr, const CNetAddr *paddrPeer)
{
	if (fNoListen)
	{
		return false;
	}

	int nBestScore = -1;
	int nBestReachability = -1;

	{
		LOCK(cs_mapLocalHost);
		
		for (std::map<CNetAddr, LocalServiceInfo>::iterator it = mapLocalHost.begin(); it != mapLocalHost.end(); it++)
		{
			int nScore = (*it).second.nScore;
			int nReachability = (*it).first.GetReachabilityFrom(paddrPeer);
			
			if (nReachability > nBestReachability || (nReachability == nBestReachability && nScore > nBestScore))
			{
				addr = CService((*it).first, (*it).second.nPort);
				nBestReachability = nReachability;
				nBestScore = nScore;
			}
		}
	}

	return nBestScore >= 0;
}

// get best local address for a particular peer as a CAddress
CAddress GetLocalAddress(const CNetAddr *paddrPeer)
{
	CAddress ret(CService("0.0.0.0",0),0);
	CService addr;

	if (GetLocal(addr, paddrPeer))
	{
		ret = CAddress(addr);
		ret.nServices = nLocalServices;
		ret.nTime = GetAdjustedTime();
	}

	return ret;
}

void RelayInventory(const CInv& inv)
{
	// Put on lists to offer to the other nodes
	{
		LOCK(cs_vNodes);
		
		for(CNode* pnode : vNodes)
		{
			pnode->PushInventory(inv);
		}
	}
}

bool RecvLine(SOCKET hSocket, std::string& strLine)
{
	strLine = "";

	while (true)
	{
		char c;
		int nBytes = recv(hSocket, &c, 1, 0);
		
		if (nBytes > 0)
		{
			if (c == '\n')
			{
				continue;
			}
			
			if (c == '\r')
			{
				return true;
			}
			
			strLine += c;
			
			if (strLine.size() >= 9000)
			{
				return true;
			}
		}
		else if (nBytes <= 0)
		{
			boost::this_thread::interruption_point();
			
			if (nBytes < 0)
			{
				int nErr = WSAGetLastError();
				if (nErr == WSAEMSGSIZE)
				{
					continue;
				}
				
				if (nErr == WSAEWOULDBLOCK || nErr == WSAEINTR || nErr == WSAEINPROGRESS)
				{
					MilliSleep(10);
					
					continue;
				}
			}
			
			if (!strLine.empty())
			{
				return true;
			}
			
			if (nBytes == 0)
			{
				// socket closed
				LogPrint("net", "socket closed\n");
				
				return false;
			}
			else
			{
				// socket error
				int nErr = WSAGetLastError();
				
				LogPrint("net", "recv failed: %s\n", nErr);
				
				return false;
			}
		}
	}
}

int GetnScore(const CService& addr)
{
	LOCK(cs_mapLocalHost);

	if (mapLocalHost.count(addr) == LOCAL_NONE)
	{
		return 0;
	}

	return mapLocalHost[addr].nScore;
}

// Is our peer's addrLocal potentially useful as an external IP source?
bool IsPeerAddrLocalGood(CNode *pnode)
{
	return fDiscover &&
			pnode->addr.IsRoutable() &&
			pnode->addrLocal.IsRoutable() &&
			!IsLimited(pnode->addrLocal.GetNetwork());
}

// used when scores of local addresses may have changed
// pushes better local address to peers
void static AdvertizeLocal()
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		if (pnode->fSuccessfullyConnected)
		{
			CAddress addrLocal = GetLocalAddress(&pnode->addr);
			
			if (addrLocal.IsRoutable() && (CService)addrLocal != (CService)pnode->addrLocal)
			{
				pnode->PushAddress(addrLocal);
				pnode->addrLocal = addrLocal;
			}
		}
	}
}

void SetReachable(enum Network net, bool fFlag)
{
	LOCK(cs_mapLocalHost);

	vfReachable[net] = fFlag;

	if (net == NET_IPV6 && fFlag)
	{
		vfReachable[NET_IPV4] = true;
	}
}

// learn a new local address
bool AddLocal(const CService& addr, int nScore)
{
    if (
		!addr.IsRoutable() ||
		(!fDiscover && nScore < LOCAL_MANUAL) ||
		IsLimited(addr)
	)
	{
        return false;
	}
	
    LogPrintf("AddLocal(%s,%i)\n", addr.ToString(), nScore);

    {
        LOCK(cs_mapLocalHost);
        
		bool fAlready = mapLocalHost.count(addr) > 0;
        LocalServiceInfo &info = mapLocalHost[addr];
        
		if (!fAlready || nScore >= info.nScore)
		{
            info.nScore = nScore + (fAlready ? 1 : 0);
            info.nPort = addr.GetPort();
        }
		
        SetReachable(addr.GetNetwork());
    }

    AdvertizeLocal();

    return true;
}

bool AddLocal(const CNetAddr &addr, int nScore)
{
	return AddLocal(CService(addr, GetListenPort()), nScore);
}

/** Make a particular network entirely off-limits (no automatic connects to it) */
void SetLimited(enum Network net, bool fLimited)
{
	if (net == NET_UNROUTABLE)
	{
		return;
	}

	LOCK(cs_mapLocalHost);

	vfLimited[net] = fLimited;
}

bool IsLimited(enum Network net)
{
	LOCK(cs_mapLocalHost);

	return vfLimited[net];
}

bool IsLimited(const CNetAddr &addr)
{
	return IsLimited(addr.GetNetwork());
}

/** vote for a local address */
bool SeenLocal(const CService& addr)
{
	{
		LOCK(cs_mapLocalHost);
		
		if (mapLocalHost.count(addr) == 0)
		{
			return false;
		}
		
		mapLocalHost[addr].nScore++;
	}

	AdvertizeLocal();

	return true;
}

/** check whether a given address is potentially local */
bool IsLocal(const CService& addr)
{
	LOCK(cs_mapLocalHost);

	return mapLocalHost.count(addr) > 0;
}

/** check whether a given address is in a network we can probably connect to */
bool IsReachable(const CNetAddr& addr)
{
	LOCK(cs_mapLocalHost);

	enum Network net = addr.GetNetwork();

	return vfReachable[net] && !vfLimited[net];
}

void AddressCurrentlyConnected(const CService& addr)
{
	addrman.Connected(addr);
}

CNode* FindNode(const CNetAddr& ip)
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		if ((CNetAddr)pnode->addr == ip)
		{
			return (pnode);
		}
	}

	return NULL;
}

CNode* FindNode(const CSubNet& subNet)
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		if (subNet.Match((CNetAddr)pnode->addr))
		{
			return (pnode);
		}
	}

	return NULL;
}

CNode* FindNode(const std::string &addrName)
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		if (pnode->addrName == addrName)
		{
			return (pnode);
		}
	}

	return NULL;
}

CNode* FindNode(const CService& addr)
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		if ((CService)pnode->addr == addr)
		{
			return (pnode);
		}
	}

	return NULL;
}

bool CheckNode(CAddress addrConnect)
{
	// Look for an existing connection. If found then just add it to masternode list.
	CNode* pnode = FindNode((CService)addrConnect);
	if (pnode)
	{
		return true;
	}

	// Connect
	SOCKET hSocket;
	if (ConnectSocket(addrConnect, hSocket))
	{
		LogPrint("net", "connected masternode %s\n", addrConnect.ToString());
		
		closesocket(hSocket);

/*        // Set to non-blocking
#ifdef WIN32
        u_long nOne = 1;
        if (ioctlsocket(hSocket, FIONBIO, &nOne) == SOCKET_ERROR)
            LogPrintf("ConnectSocket() : ioctlsocket non-blocking setting failed, error %d\n", WSAGetLastError());
#else
        if (fcntl(hSocket, F_SETFL, O_NONBLOCK) == SOCKET_ERROR)
            LogPrintf("ConnectSocket() : fcntl non-blocking setting failed, error %d\n", errno);
#endif
        CNode* pnode = new CNode(hSocket, addrConnect, "", false);
        // Close connection
        pnode->CloseSocketDisconnect();
*/
		return true;
	}

	LogPrint("net", "connecting to masternode %s failed\n", addrConnect.ToString());

	return false;
}

CNode* ConnectNode(CAddress addrConnect, const char *pszDest, bool mnEngineMaster)
{
	if (pszDest == NULL)
	{
		if (IsLocal(addrConnect))
		{
			return NULL;
		}
		
		// Look for an existing connection
		CNode* pnode = FindNode((CService)addrConnect);
		if (pnode)
		{
			if(mnEngineMaster)
			{
				pnode->fMNengineMaster = true;
			}
			
			pnode->AddRef();
			
			return pnode;
		}
	}

	/// debug print
	LogPrint("net", "trying connection %s lastseen=%.1fhrs\n",
		pszDest ? pszDest : addrConnect.ToString(),
		pszDest ? 0 : (double)(GetAdjustedTime() - addrConnect.nTime)/3600.0
	);

	// Connect
	SOCKET hSocket;
	
	if (pszDest ? ConnectSocketByName(addrConnect, hSocket, pszDest, Params().GetDefaultPort()) : ConnectSocket(addrConnect, hSocket))
	{
		addrman.Attempt(addrConnect);

		LogPrint("net", "connected %s\n", pszDest ? pszDest : addrConnect.ToString());

		// Set to non-blocking
#ifdef WIN32
		u_long nOne = 1;
		if (ioctlsocket(hSocket, FIONBIO, &nOne) == SOCKET_ERROR)
		{
			LogPrintf("ConnectSocket() : ioctlsocket non-blocking setting failed, error %d\n", WSAGetLastError());
		}
#else
		if (fcntl(hSocket, F_SETFL, O_NONBLOCK) == SOCKET_ERROR)
		{
			LogPrintf("ConnectSocket() : fcntl non-blocking setting failed, error %d\n", errno);
		}
#endif

		// Add node
		CNode* pnode = new CNode(hSocket, addrConnect, pszDest ? pszDest : "", false);
		pnode->AddRef();

		{
			LOCK(cs_vNodes);
			
			vNodes.push_back(pnode);
		}

		pnode->nTimeConnected = GetTime();
		
		return pnode;
	}
	else
	{
		return NULL;
	}
}

void RefreshRecentConnections(int RefreshMinutes)
{
	if (vNodes.size() >= 8)
	{
		return;
	}

	time_t timer;
	int SecondsPassed = 0;
	int MinutesPassed = 0;
	int CurrentTimestamp = time(&timer);

	if (LastRefreshstamp > 0)
	{
		SecondsPassed = CurrentTimestamp - LastRefreshstamp;
		MinutesPassed = SecondsPassed / 60;

		if (MinutesPassed > RefreshMinutes - 2) 
		{
			FirstCycle = false;
		}
	}
	else
	{
		LastRefreshstamp = CurrentTimestamp;
		
		return;
	}

	if (FirstCycle == false)
	{
		if (MinutesPassed < RefreshMinutes) 
		{
			return;
		}
		else
		{
			RefreshesDone = RefreshesDone + 1;

			//cout<<"         Last refresh: "<<LastRefreshstamp<<endl;
			//cout<<"         Minutes ago: "<<MinutesPassed<<endl;
			//cout<<"         Peer/node refresh cycles: "<<RefreshesDone<<endl;

			LastRefreshstamp = CurrentTimestamp;

			// Load addresses for peers.dat
			int64_t nStart = GetTimeMillis();
			{
				CAddrDB adb;
				
				if (!adb.Read(addrman))
				{
					LogPrintf("Invalid or missing peers.dat; recreating\n");
				}
			}
			
			LogPrintf("Loaded %i addresses from peers.dat  %dms\n",
			
			addrman.size(), GetTimeMillis() - nStart);

			const std::vector<CDNSSeedData> &vSeeds = Params().DNSSeeds();
			int found = 0;
			   
			LogPrintf("Loading addresses from DNS seeds (could take a while)\n");

			for(const CDNSSeedData &seed : vSeeds)
			{
				if (HaveNameProxy())
				{
					AddOneShot(seed.host);
				} 
				else 
				{
					std::vector<CNetAddr> vIPs;
					std::vector<CAddress> vAdd;
					
					if (LookupHost(seed.host.c_str(), vIPs))
					{
						for(CNetAddr& ip : vIPs)
						{
							if (found < 16)
							{
								int nOneDay = 24*3600;
								CAddress addr = CAddress(CService(ip, Params().GetDefaultPort()));
								addr.nTime = GetTime() - 3*nOneDay - GetRand(4*nOneDay); // use a random age between 3 and 7 days old
								vAdd.push_back(addr);
								
								found++;
							}
						}
					}
					
					addrman.Add(vAdd, CNetAddr(seed.name, true));
				}
			}

			LogPrintf("%d addresses found from DNS seeds\n", found);

			//DumpAddresses();

			CSemaphoreGrant grant(*semOutbound);
			boost::this_thread::interruption_point();

			// Choose an address to connect to based on most recently seen
			//
			CAddress addrConnect;

			// Only connect out to one peer per network group (/16 for IPv4).
			// Do this here so we don't have to critsect vNodes inside mapAddresses critsect.
			int nOutbound = 0;
			std::set<std::vector<unsigned char> > setConnected;
			
			{
				LOCK(cs_vNodes);
				
				for(CNode* pnode : vNodes)
				{
					if (!pnode->fInbound)
					{
						setConnected.insert(pnode->addr.GetGroup());
						
						nOutbound++;
					}
				}
			}

			int64_t nANow = GetAdjustedTime();

			int nTries = 0;
			while (true)
			{
				CAddress addr = addrman.Select();

				// if we selected an invalid address, restart
				if (!addr.IsValid() || setConnected.count(addr.GetGroup()) || IsLocal(addr))
				{
					break;
				}

				// If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
				// stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
				// already-connected network ranges, ...) before trying new addrman addresses.
				nTries++;
				if (nTries > 100)
				{
					break;
				}

				// only consider very recently tried nodes after 30 failed attempts
				// do not allow non-default ports, unless after 50 invalid addresses selected already
				if (IsLimited(addr) ||
					(nANow - addr.nLastTry < 600 && nTries < 30) ||
					(addr.GetPort() != Params().GetDefaultPort() && nTries < 50)
				)
				{
					continue;
				}

				addrConnect = addr;
				
				break;
			}

			if (addrConnect.IsValid())
			{
				OpenNetworkConnection(addrConnect, &grant);
			}
		}
	}

	return;
}

void IdleNodeCheck(CNode *pnode)
{
	// Disconnect node/peer if send/recv data becomes idle
	if (GetTime() - pnode->nTimeConnected > 60)
	{
		if (GetTime() - pnode->nLastRecv > 60)
		{
			if (GetTime() - pnode->nLastSend < 60)
			{
				LogPrintf("Error: Unexpected idle interruption %s\n", pnode->addrName);
				
				pnode->CloseSocketDisconnect();
			}
		}
	}

	return;
}

// requires LOCK(cs_vSend)
void SocketSendData(CNode *pnode)
{
	std::deque<CSerializeData>::iterator it = pnode->vSendMsg.begin();

	while (it != pnode->vSendMsg.end())
	{
		const CSerializeData &data = *it;
		
		assert(data.size() > pnode->nSendOffset);
		
		int nBytes = send(pnode->hSocket, &data[pnode->nSendOffset], data.size() - pnode->nSendOffset, MSG_NOSIGNAL | MSG_DONTWAIT);
		
		if (nBytes > 0)
		{
			pnode->nLastSend = GetTime();
			pnode->nSendBytes += nBytes;
			pnode->nSendOffset += nBytes;
			pnode->RecordBytesSent(nBytes);
			
			if (pnode->nSendOffset == data.size())
			{
				pnode->nSendOffset = 0;
				pnode->nSendSize -= data.size();
				
				it++;
			}
			else
			{
				// could not send full message; stop sending more
				LogPrintf("socket send error: interruption\n");
				
				IdleNodeCheck(pnode);
				
				break;
			}
		}
		else
		{
			if (nBytes == 0)
			{
				// couldn't send anything at all
				LogPrintf("socket send error: data failure\n");
				
				pnode->CloseSocketDisconnect();
				
				break;
			}

			pnode->CloseSocketDisconnect();
			
			break;
		}
	}

	if (it == pnode->vSendMsg.end())
	{
		assert(pnode->nSendOffset == 0);
		assert(pnode->nSendSize == 0);
	}

	pnode->vSendMsg.erase(pnode->vSendMsg.begin(), it);
}

void ThreadSocketHandler()
{
	unsigned int nPrevNodeCount = 0;

	while (true)
	{
		//
		// Disconnect nodes
		//
		{
			LOCK(cs_vNodes);
			
			// Disconnect unused nodes
			std::vector<CNode*> vNodesCopy = vNodes;
			
			for(CNode* pnode : vNodesCopy)
			{
				if (pnode->fDisconnect ||
					(
						pnode->GetRefCount() <= 0 &&
						pnode->vRecvMsg.empty() && pnode->nSendSize == 0 &&
						pnode->ssSend.empty()
					)
				)
				{
					// remove from vNodes
					vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());

					// release outbound grant (if any)
					pnode->grantOutbound.Release();

					// close socket and cleanup
					pnode->CloseSocketDisconnect();

					// hold in disconnected pool until all refs are released
					if (pnode->fNetworkNode || pnode->fInbound)
					{
						pnode->Release();
					}
					
					vNodesDisconnected.push_back(pnode);
				}
			}
		}
		
		{
			// Delete disconnected nodes
			std::list<CNode*> vNodesDisconnectedCopy = vNodesDisconnected;
			
			for(CNode* pnode : vNodesDisconnectedCopy)
			{
				// wait until threads are done using it
				if (pnode->GetRefCount() <= 0)
				{
					bool fDelete = false;
					
					{
						TRY_LOCK(pnode->cs_vSend, lockSend);
						
						if (lockSend)
						{
							TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
							
							if (lockRecv)
							{
								TRY_LOCK(pnode->cs_inventory, lockInv);
								
								if (lockInv)
								{
									fDelete = true;
								}
							}
						}
					}
					
					if (fDelete)
					{
						vNodesDisconnected.remove(pnode);
						
						delete pnode;
					}
				}
			}
		}
		
		if(vNodes.size() != nPrevNodeCount)
		{
			nPrevNodeCount = vNodes.size();
			uiInterface.NotifyNumConnectionsChanged(nPrevNodeCount);
		}
		
		//
		// Find which sockets have data to receive
		//
		struct timeval timeout;
		timeout.tv_sec  = 0;
		timeout.tv_usec = 50000; // frequency to poll pnode->vSend

		fd_set fdsetRecv;
		fd_set fdsetSend;
		fd_set fdsetError;
		FD_ZERO(&fdsetRecv);
		FD_ZERO(&fdsetSend);
		FD_ZERO(&fdsetError);
		SOCKET hSocketMax = 0;
		bool have_fds = false;

		for(SOCKET hListenSocket : vhListenSocket)
		{
			FD_SET(hListenSocket, &fdsetRecv);
			hSocketMax = std::max(hSocketMax, hListenSocket);
			have_fds = true;
		}
		
		{
			LOCK(cs_vNodes);
			
			for(CNode* pnode : vNodes)
			{
				if (pnode->hSocket == INVALID_SOCKET)
				{
					continue;
				}
			
				FD_SET(pnode->hSocket, &fdsetError);
				hSocketMax = std::max(hSocketMax, pnode->hSocket);
				have_fds = true;

				// Implement the following logic:
				// * If there is data to send, select() for sending data. As this only
				//   happens when optimistic write failed, we choose to first drain the
				//   write buffer in this case before receiving more. This avoids
				//   needlessly queueing received data, if the remote peer is not themselves
				//   receiving data. This means properly utilizing TCP flow control signalling.
				// * Otherwise, if there is no (complete) message in the receive buffer,
				//   or there is space left in the buffer, select() for receiving data.
				// * (if neither of the above applies, there is certainly one message
				//   in the receiver buffer ready to be processed).
				// Together, that means that at least one of the following is always possible,
				// so we don't deadlock:
				// * We send some data.
				// * We wait for data to be received (and disconnect after timeout).
				// * We process a message in the buffer (message handler thread).
				{
					TRY_LOCK(pnode->cs_vSend, lockSend);
					
					if (lockSend && !pnode->vSendMsg.empty())
					{
						FD_SET(pnode->hSocket, &fdsetSend);
						
						continue;
					}
				}
				
				{
					TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
					
					if (lockRecv &&
						(
							pnode->vRecvMsg.empty() ||
							!pnode->vRecvMsg.front().complete() ||
							pnode->GetTotalRecvSize() <= ReceiveFloodSize()
						)
					)
					{
						FD_SET(pnode->hSocket, &fdsetRecv);
					}
				}
			}
		}

		int nSelect = select(have_fds ? hSocketMax + 1 : 0,&fdsetRecv, &fdsetSend, &fdsetError, &timeout);
		boost::this_thread::interruption_point();

		if (nSelect == SOCKET_ERROR)
		{
			if (have_fds)
			{
				int nErr = WSAGetLastError();
				
				LogPrintf("socket select error %d\n", nErr);
				
				for (unsigned int i = 0; i <= hSocketMax; i++)
				{
					FD_SET(i, &fdsetRecv);
				}
			}
			
			FD_ZERO(&fdsetSend);
			FD_ZERO(&fdsetError);
			
			MilliSleep(timeout.tv_usec/1000);
		}
		
		//
		// Accept new connections
		//
		for(SOCKET hListenSocket : vhListenSocket)
		{
			if (hListenSocket != INVALID_SOCKET && FD_ISSET(hListenSocket, &fdsetRecv))
			{
				struct sockaddr_storage sockaddr;
				socklen_t len = sizeof(sockaddr);
				SOCKET hSocket = accept(hListenSocket, (struct sockaddr*)&sockaddr, &len);
				CAddress addr;
				int nInbound = 0;

				if (hSocket != INVALID_SOCKET)
				{
					if (!addr.SetSockAddr((const struct sockaddr*)&sockaddr))
					{
						LogPrintf("Warning: Unknown socket family\n");
					}
				}
				
				{
					LOCK(cs_vNodes);
					
					for(CNode* pnode : vNodes)
					{
						if (pnode->fInbound)
						{
							nInbound++;
						}
					}
				}
				if (hSocket == INVALID_SOCKET)
				{
					int nErr = WSAGetLastError();
					if (nErr != WSAEWOULDBLOCK)
					{
						LogPrintf("socket error accept failed: %d\n", nErr);
					}
				}
				else if (nInbound >= nMaxConnections - MAX_OUTBOUND_CONNECTIONS)
				{
					closesocket(hSocket);
				}
				else if (CNode::IsBanned(addr))
				{
					LogPrintf("connection from %s dropped (banned)\n", addr.ToString());
					
					closesocket(hSocket);
				}
				else
				{
					// According to the internet TCP_NODELAY is not carried into accepted sockets
					// on all platforms.  Set it again here just to be sure.
					int set = 1;
#ifdef WIN32
					setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&set, sizeof(int));
#else // WIN32
					setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (void*)&set, sizeof(int));
#endif // WIN32

					LogPrint("net", "accepted connection %s\n", addr.ToString());
					
					CNode* pnode = new CNode(hSocket, addr, "", true);
					pnode->AddRef();
					
					{
						LOCK(cs_vNodes);
						
						vNodes.push_back(pnode);
					}
				}
			}
		}
		
		//
		// Service each socket
		//
		std::vector<CNode*> vNodesCopy;
		
		{
			LOCK(cs_vNodes);
			
			vNodesCopy = vNodes;
			
			for(CNode* pnode : vNodesCopy)
			{
				pnode->AddRef();
			}
		}
		
		for(CNode* pnode : vNodesCopy)
		{
			boost::this_thread::interruption_point();

			//
			// Receive
			//
			if (pnode->hSocket == INVALID_SOCKET)
			{
				continue;
			}
			
			if (FD_ISSET(pnode->hSocket, &fdsetRecv) || FD_ISSET(pnode->hSocket, &fdsetError))
			{
				TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
				
				if (lockRecv)
				{
					if (pnode->GetTotalRecvSize() > ReceiveFloodSize())
					{
						if (!pnode->fDisconnect)
						{
							LogPrintf("socket recv flood control disconnect (%u bytes)\n", pnode->GetTotalRecvSize());
						}
						
						pnode->CloseSocketDisconnect();
					}
					else
					{
						// typical socket buffer is 8K-64K
						char pchBuf[0x10000];
						int nBytes = recv(pnode->hSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
						
						if (nBytes > 0)
						{
							if (!pnode->ReceiveMsgBytes(pchBuf, nBytes))
							{
								pnode->CloseSocketDisconnect();
							}
							
							pnode->nLastRecv = GetTime();
							pnode->nRecvBytes += nBytes;
							pnode->RecordBytesRecv(nBytes);
						}
						else if (nBytes == 0)
						{
							// socket closed gracefully
							if (!pnode->fDisconnect)
							{
								LogPrint("net", "socket closed\n");
							}
							
							pnode->CloseSocketDisconnect();
						}
						else if (nBytes < 0)
						{
							// error
							int nErr = WSAGetLastError();
							if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
							{
								{
									LogPrintf("ThreadSocketHandler() : (ERROR) invalid data from peer %d, socket recv error %s \n", pnode->addr.ToString(), nErr);
								}
								// Disconnect from node that sent us invalid data
								// This is not a ban
								pnode->CloseSocketDisconnect();
							}
						}
					}
				}
			}

			//
			// Send
			//
			if (pnode->hSocket == INVALID_SOCKET)
			{
				continue;
			}
			
			if (FD_ISSET(pnode->hSocket, &fdsetSend))
			{
				TRY_LOCK(pnode->cs_vSend, lockSend);
				
				if (lockSend)
				{
					SocketSendData(pnode);
				}
			}

			//
			// Inactivity checking
			//
			if (pnode->vSendMsg.empty())
			{
				pnode->nLastSendEmpty = GetTime();
			}

			if (GetTime() - pnode->nTimeConnected > IDLE_TIMEOUT)
			{
				// First see if we've received/sent anything
				if (pnode->nLastRecv == 0 || pnode->nLastSend == 0)
				{
					// Disconnect if we have a completely stale connection
					LogPrint("net", "socket no message in timeout, %d %d\n", pnode->nLastRecv != 0, pnode->nLastSend != 0);
					
					pnode->fDisconnect = true;
					pnode->CloseSocketDisconnect();
				}
				// Send timeout
				else if (GetTime() - pnode->nLastSend > DATA_TIMEOUT)
				{
					LogPrintf("socket not sending\n");
					
					pnode->fDisconnect = true;
					pnode->CloseSocketDisconnect();
				}
				// Receive timeout
				else if (GetTime() - pnode->nLastRecv > DATA_TIMEOUT)
				{
					LogPrintf("socket inactivity timeout\n");
					
					pnode->fDisconnect = true;
					pnode->CloseSocketDisconnect();
				}
				// Ping timeout - TODO : Review function
				else if (pnode->nPingNonceSent && pnode->nPingUsecStart + TIMEOUT_INTERVAL * 1000000 < GetTimeMicros())
				{
					LogPrintf("ping timeout: %fs\n", 0.000001 * (GetTimeMicros() - pnode->nPingUsecStart));
					
					pnode->fDisconnect = true;
					pnode->CloseSocketDisconnect();
				}
			}
		}
		
		{
			LOCK(cs_vNodes);
			
			for(CNode* pnode : vNodesCopy)
			{
				pnode->Release();
			}
		}
	}

	// Refresh nodes/peers every X minutes
	RefreshRecentConnections(20);
}

#ifdef USE_UPNP
void ThreadMapPort()
{
	std::string port = strprintf("%u", GetListenPort());
	const char * multicastif = 0;
	const char * minissdpdpath = 0;
	struct UPNPDev * devlist = 0;
	char lanaddr[64];

#ifndef UPNPDISCOVER_SUCCESS
	/* miniupnpc 1.5 */
	devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0);
#elif MINIUPNPC_API_VERSION < 14
	/* miniupnpc 1.6 */
	int error = 0;
	devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, &error);
#else // UPNPDISCOVER_SUCCESS && MINIUPNPC_API_VERSION < 14
	/* miniupnpc 1.9.20150730 */
	int error = 0;
	devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, 2, &error);
#endif // UPNPDISCOVER_SUCCESS && MINIUPNPC_API_VERSION < 14

	struct UPNPUrls urls;
	struct IGDdatas data;
	int r;

	r = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));

	if (r == 1)
	{
		if (fDiscover)
		{
			char externalIPAddress[40];
			r = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIPAddress);
			
			if(r != UPNPCOMMAND_SUCCESS)
			{
				LogPrintf("UPnP: GetExternalIPAddress() returned %d\n", r);
			}
			else
			{
				if(externalIPAddress[0])
				{
					LogPrintf("UPnP: ExternalIPAddress = %s\n", externalIPAddress);
					
					AddLocal(CNetAddr(externalIPAddress), LOCAL_UPNP);
				}
				else
				{
					LogPrintf("UPnP: GetExternalIPAddress failed.\n");
				}
			}
		}

		std::string strDesc = "DigitalNote " + FormatFullVersion();

		try
		{
			while (!ShutdownRequested())
			{
				boost::this_thread::interruption_point();

#ifndef UPNPDISCOVER_SUCCESS
				/* miniupnpc 1.5 */
				r = UPNP_AddPortMapping(
					urls.controlURL,
					data.first.servicetype,
					port.c_str(),
					port.c_str(),
					lanaddr,
					strDesc.c_str(),
					"TCP",
					0
				);
#else // UPNPDISCOVER_SUCCESS
				/* miniupnpc 1.6 */
				r = UPNP_AddPortMapping(
					urls.controlURL,
					data.first.servicetype,
					port.c_str(),
					port.c_str(),
					lanaddr,
					strDesc.c_str(),
					"TCP",
					0,
					"0"
				);
#endif // UPNPDISCOVER_SUCCESS

				if(r != UPNPCOMMAND_SUCCESS)
				{
					LogPrintf(
						"AddPortMapping(%s, %s, %s) failed with code %d (%s)\n",
						port,
						port,
						lanaddr,
						r,
						strupnperror(r)
					);
				}
				else
				{
					LogPrintf("UPnP Port Mapping successful.\n");;
				}
				
				MilliSleep(20*60*1000); // Refresh every 20 minutes
			}
		}
		catch (boost::thread_interrupted)
		{
			r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port.c_str(), "TCP", 0);
			
			LogPrintf("UPNP_DeletePortMapping() returned : %d\n", r);
			
			freeUPNPDevlist(devlist);
			devlist = 0;
			FreeUPNPUrls(&urls);
			
			throw;
		}
	}
	else
	{
		LogPrintf("No valid UPnP IGDs found\n");
		
		freeUPNPDevlist(devlist); devlist = 0;
		
		if (r != 0)
		{
			FreeUPNPUrls(&urls);
		}
	}
}

void MapPort(bool fUseUPnP)
{
	static boost::thread* upnp_thread = NULL;

	if (fUseUPnP)
	{
		if (upnp_thread)
		{
			// Check if the thread is still running or not
			bool fThreadStopped = upnp_thread->timed_join(boost::posix_time::seconds(0));
			if (fThreadStopped)
			{
				delete upnp_thread;
				
				upnp_thread = NULL;
			}
		}
		
		if (!upnp_thread)
		{
			// Start the UPnP thread if not running
			upnp_thread = new boost::thread(boost::bind(&TraceThread<void (*)()>, "upnp", &ThreadMapPort));
		}
	}
	else if (upnp_thread)
	{
		upnp_thread->interrupt();
		
		if (ShutdownRequested())
		{
			// Only wait for the thread to finish if a shutdown is requested
			upnp_thread->join();
			
			delete upnp_thread;
			
			upnp_thread = NULL;
		}
	}
}

#else // USE_UPNP
void MapPort(bool)
{
    // Intentionally left blank.
}
#endif // USE_UPNP

void ThreadDNSAddressSeed()
{
	// goal: only query DNS seeds if address need is acute
	if ((addrman.size() > 0) && (!GetBoolArg("-forcednsseed", false)))
	{
		MilliSleep(11 * 1000);

		LOCK(cs_vNodes);
		
		if (vNodes.size() >= 2)
		{
			LogPrintf("P2P peers available. Skipped DNS seeding.\n");
			
			return;
		}
	}

	const std::vector<CDNSSeedData> &vSeeds = Params().DNSSeeds();
	int found = 0;

	LogPrintf("Loading addresses from DNS seeds (could take a while)\n");

	for(const CDNSSeedData &seed : vSeeds)
	{
		if (HaveNameProxy())
		{
			AddOneShot(seed.host);
		}
		else
		{
			std::vector<CNetAddr> vIPs;
			std::vector<CAddress> vAdd;
			
			if (LookupHost(seed.host.c_str(), vIPs))
			{
				for(CNetAddr& ip : vIPs)
				{
					int nOneDay = 24*3600;
					CAddress addr = CAddress(CService(ip, Params().GetDefaultPort()));
					addr.nTime = GetTime() - 3*nOneDay - GetRand(4*nOneDay); // use a random age between 3 and 7 days old
					
					vAdd.push_back(addr);
					
					found++;
				}
			}
			
			addrman.Add(vAdd, CNetAddr(seed.name, true));
		}
	}

	LogPrintf("%d addresses found from DNS seeds\n", found);
}

void DumpAddresses()
{
	int64_t nStart = GetTimeMillis();
	CAddrDB adb;

	adb.Write(addrman);

	LogPrint("net", "Flushed %d addresses to peers.dat  %dms\n",
		addrman.size(),
		GetTimeMillis() - nStart
	);
}

void DumpData()
{
	DumpAddresses();

	if (CNode::BannedSetIsDirty())
	{
		DumpBanlist();
		CNode::SetBannedSetDirty(false);
	}
}

void static ProcessOneShot()
{
	std::string strDest;

	{
		LOCK(cs_vOneShots);
		
		if (vOneShots.empty())
		{
			return;
		}

		strDest = vOneShots.front();
		vOneShots.pop_front();
	}

	CAddress addr;
	CSemaphoreGrant grant(*semOutbound, true);

	if (grant)
	{
		if (!OpenNetworkConnection(addr, &grant, strDest.c_str(), true))
		{
			AddOneShot(strDest);
		}
	}
}

void ThreadOpenConnections()
{
	// Connect to specific addresses
	if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0)
	{
		for (int64_t nLoop = 0;; nLoop++)
		{
			ProcessOneShot();
			
			for(std::string strAddr : mapMultiArgs["-connect"])
			{
				CAddress addr;
				
				OpenNetworkConnection(addr, NULL, strAddr.c_str());
				
				for (int i = 0; i < 10 && i < nLoop; i++)
				{
					MilliSleep(500);
				}
			}
			
			MilliSleep(500);
		}
	}

	// Initiate network connections
	int64_t nStart = GetTime();
	
	while (true)
	{
		ProcessOneShot();

		MilliSleep(500);

		CSemaphoreGrant grant(*semOutbound);
		boost::this_thread::interruption_point();

		// Add seed nodes if DNS seeds are all down (an infrastructure attack?).
		if (addrman.size() == 0 && (GetTime() - nStart > 60))
		{
			static bool done = false;
			
			if (!done)
			{
				LogPrintf("Adding fixed seed nodes as DNS doesn't seem to be available.\n");
				
				addrman.Add(Params().FixedSeeds(), CNetAddr("127.0.0.1"));
				done = true;
			}
		}

		//
		// Choose an address to connect to based on most recently seen
		//
		CAddress addrConnect;

		// Only connect out to one peer per network group (/16 for IPv4).
		// Do this here so we don't have to critsect vNodes inside mapAddresses critsect.
		int nOutbound = 0;
		std::set<std::vector<unsigned char> > setConnected;
		
		{
			LOCK(cs_vNodes);
			
			for(CNode* pnode : vNodes)
			{
				if (!pnode->fInbound)
				{
					setConnected.insert(pnode->addr.GetGroup());
					
					nOutbound++;
				}
			}
		}

		int64_t nANow = GetAdjustedTime();
		int nTries = 0;
		
		while (true)
		{
			// use an nUnkBias between 10 (no outgoing connections) and 90 (8 outgoing connections)
			CAddress addr = addrman.Select(10 + std::min(nOutbound,8)*10);

			// if we selected an invalid address, restart
			if (!addr.IsValid() || setConnected.count(addr.GetGroup()) || IsLocal(addr))
			{
				break;
			}
			
			// If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
			// stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
			// already-connected network ranges, ...) before trying new addrman addresses.
			nTries++;
			if (nTries > 100)
			{
				break;
			}
			
			// only consider very recently tried nodes after 30 failed attempts
			// do not allow non-default ports, unless after 50 invalid addresses selected already
			if (IsLimited(addr) ||
				(nANow - addr.nLastTry < 600 && nTries < 30) ||
				(addr.GetPort() != Params().GetDefaultPort() && nTries < 50)
			)
			{
				continue;
			}
			
			addrConnect = addr;
			
			break;
		}

		if (addrConnect.IsValid())
		{
			OpenNetworkConnection(addrConnect, &grant);
		}
	}
}

void ThreadOpenAddedConnections()
{
	{
		LOCK(cs_vAddedNodes);
		
		vAddedNodes = mapMultiArgs["-addnode"];
	}

	if (HaveNameProxy())
	{
		while(true)
		{
			std::list<std::string> lAddresses(0);
			
			{
				LOCK(cs_vAddedNodes);
				
				for(std::string& strAddNode : vAddedNodes)
				{
					lAddresses.push_back(strAddNode);
				}
			}
			
			for(std::string& strAddNode : lAddresses)
			{
				CAddress addr;
				CSemaphoreGrant grant(*semOutbound);
				
				OpenNetworkConnection(addr, &grant, strAddNode.c_str());
				
				MilliSleep(500);
			}
			
			MilliSleep(120000); // Retry every 2 minutes
		}
	}

	for (unsigned int i = 0; true; i++)
	{
		std::list<std::string> lAddresses(0);
		
		{
			LOCK(cs_vAddedNodes);
			
			for(std::string& strAddNode : vAddedNodes)
			{
				lAddresses.push_back(strAddNode);
			}
		}

		std::list<std::vector<CService> > lservAddressesToAdd(0);
		
		for(std::string& strAddNode : lAddresses)
		{
			std::vector<CService> vservNode(0);
			
			if(Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), fNameLookup, 0))
			{
				lservAddressesToAdd.push_back(vservNode);
				
				{
					LOCK(cs_setservAddNodeAddresses);
					
					for(CService& serv : vservNode)
					{
						setservAddNodeAddresses.insert(serv);
					}
				}
			}
		}
		
		// Attempt to connect to each IP for each addnode entry until at least one is successful per addnode entry
		// (keeping in mind that addnode entries can have many IPs if fNameLookup)
		{
			LOCK(cs_vNodes);
			
			for(CNode* pnode : vNodes)
			{
				for (std::list<std::vector<CService> >::iterator it = lservAddressesToAdd.begin(); it != lservAddressesToAdd.end(); it++)
				{   
					for(CService& addrNode : *(it))
					{
						if (pnode->addr == addrNode)
						{
							it = lservAddressesToAdd.erase(it);
							it--;
							
							break;
						}
					}
				}
			}
		}
		
		for(std::vector<CService>& vserv : lservAddressesToAdd)
		{
			CSemaphoreGrant grant(*semOutbound);
			
			OpenNetworkConnection(CAddress(vserv[i % vserv.size()]), &grant);
			
			MilliSleep(500);
		}
		
		MilliSleep(120000); // Retry every 2 minutes
	}
}

// if successful, this moves the passed grant to the constructed node
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound, const char *strDest, bool fOneShot)
{
	//
	// Initiate outbound network connection
	//
	boost::this_thread::interruption_point();

	if (!strDest)
	{
		if(IsLocal(addrConnect) ||
			FindNode((CNetAddr)addrConnect) || CNode::IsBanned(addrConnect) ||
			FindNode(addrConnect.ToStringIPPort().c_str()))
		{
			return false;
		}
	}

	if (strDest && FindNode(strDest))
	{
		return false;
	}

	CNode* pnode = ConnectNode(addrConnect, strDest);
	boost::this_thread::interruption_point();

	if (!pnode)
	{
		return false;
	}

	if (grantOutbound)
	{
		grantOutbound->MoveTo(pnode->grantOutbound);
	}

	pnode->fNetworkNode = true;

	if (fOneShot)
	{
		pnode->fOneShot = true;
	}

	return true;
}

// for now, use a very simple selection metric: the node from which we received
// most recently
static int64_t NodeSyncScore(const CNode *pnode)
{
	return pnode->nLastRecv;
}

void static StartSync(const std::vector<CNode*> &vNodes)
{
	CNode *pnodeNewSync = NULL;
	int64_t nBestScore = 0;

	// fImporting and fReindex are accessed out of cs_main here, but only
	// as an optimization - they are checked again in SendMessages.
	if (fImporting || fReindex)
	{
		return;
	}

	// Iterate over all nodes
	for(CNode* pnode : vNodes)
	{
		// check preconditions for allowing a sync
		if (!pnode->fClient && !pnode->fOneShot &&
			!pnode->fDisconnect && pnode->fSuccessfullyConnected &&
			(pnode->nStartingHeight > (nBestHeight - 144)) &&
			(pnode->nVersion < NOBLKS_VERSION_START || pnode->nVersion >= NOBLKS_VERSION_END))
		{
			// if ok, compare node's score with the best so far
			int64_t nScore = NodeSyncScore(pnode);
			
			if (pnodeNewSync == NULL || nScore > nBestScore)
			{
				pnodeNewSync = pnode;
				nBestScore = nScore;
			}
		}
	}

	// if a new sync candidate was found, start sync!
	if (pnodeNewSync)
	{
		pnodeNewSync->fStartSync = true;
		pnodeSync = pnodeNewSync;
	}
}

void ThreadMessageHandler()
{
	SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);

	while(true)
	{
		bool fHaveSyncNode = false;
		std::vector<CNode*> vNodesCopy;
		
		{
			LOCK(cs_vNodes);
			
			vNodesCopy = vNodes;
			
			for(CNode* pnode : vNodesCopy)
			{
				pnode->AddRef();
				
				if (pnode == pnodeSync)
				{
					fHaveSyncNode = true;
				}
			}
		}

		if (!fHaveSyncNode)
		{
			StartSync(vNodesCopy);
		}
		
		// Poll the connected nodes for messages
		CNode* pnodeTrickle = NULL;
		if (!vNodesCopy.empty())
		{
			pnodeTrickle = vNodesCopy[GetRand(vNodesCopy.size())];
		}
		
		bool fSleep = true;

		for(CNode* pnode : vNodesCopy)
		{
			if (pnode->fDisconnect)
			{
				continue;
			}
			
			// Receive messages
			{
				TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
				
				if (lockRecv)
				{
					if (!g_net_signals.ProcessMessages(pnode))
					{
						pnode->CloseSocketDisconnect();
					}

					// Disconnect node/peer if send/recv data becomes idle
					if (GetTime() - pnode->nTimeConnected > 90)
					{
						if (GetTime() - pnode->nLastRecv > 60)
						{
							if (GetTime() - pnode->nLastSend < 30)
							{
								LogPrintf("Error: Unexpected idle interruption %s\n", pnode->addrName);
								
								pnode->CloseSocketDisconnect();
							}
						}
					}

					if (pnode->nSendSize < SendBufferSize())
					{
						if (!pnode->vRecvGetData.empty() || (!pnode->vRecvMsg.empty() && pnode->vRecvMsg[0].complete()))
						{
							fSleep = false;
						}
					}
				}
			}
			
			boost::this_thread::interruption_point();

			// Send messages
			{
				TRY_LOCK(pnode->cs_vSend, lockSend);
				
				if (lockSend)
				{
					g_net_signals.SendMessages(pnode, pnode == pnodeTrickle);
				}
			}
			
			boost::this_thread::interruption_point();
		}

		{
			LOCK(cs_vNodes);
			
			for(CNode* pnode : vNodesCopy)
			{
				pnode->Release();
			}
		}

		if (fSleep)
		{
			MilliSleep(100);
		}
	}
}

bool BindListenPort(const CService &addrBind, std::string& strError)
{
	strError = "";
	int nOne = 1;

#ifdef WIN32
	// Initialize Windows Sockets
	WSADATA wsadata;
	int ret = WSAStartup(MAKEWORD(2,2), &wsadata);

	if (ret != NO_ERROR)
	{
		strError = strprintf("Error: TCP/IP socket library failed to start (WSAStartup returned error %d)", ret);
		
		LogPrintf("%s\n", strError);
		
		return false;
	}
#endif // WIN32

	// Create socket for listening for incoming connections
	struct sockaddr_storage sockaddr;
	socklen_t len = sizeof(sockaddr);

	if (!addrBind.GetSockAddr((struct sockaddr*)&sockaddr, &len))
	{
		strError = strprintf("Error: bind address family for %s not supported", addrBind.ToString());
		
		LogPrintf("%s\n", strError);
		
		return false;
	}

	SOCKET hListenSocket = socket(((struct sockaddr*)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
	
	if (hListenSocket == INVALID_SOCKET)
	{
		strError = strprintf("Error: Couldn't open socket for incoming connections (socket returned error %d)", WSAGetLastError());
		
		LogPrintf("%s\n", strError);
		
		return false;
	}

#ifndef WIN32
	#ifdef SO_NOSIGPIPE
	// Different way of disabling SIGPIPE on BSD
	setsockopt(hListenSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&nOne, sizeof(int));
	#endif // SO_NOSIGPIPE

	// Allow binding if the port is still in TIME_WAIT state after
	// the program was closed and restarted.  Not an issue on windows.
	setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (void*)&nOne, sizeof(int));
	// Disable Nagle's algorithm
	setsockopt(hListenSocket, IPPROTO_TCP, TCP_NODELAY, (void*)&nOne, sizeof(int));
#else // WIN32
	setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nOne, sizeof(int));
	setsockopt(hListenSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nOne, sizeof(int));
#endif // WIN32


#ifdef WIN32
	// Set to non-blocking, incoming connections will also inherit this
	if (ioctlsocket(hListenSocket, FIONBIO, (u_long*)&nOne) == SOCKET_ERROR)
#else // WIN32
	if (fcntl(hListenSocket, F_SETFL, O_NONBLOCK) == SOCKET_ERROR)
#endif // WIN32

	{
		strError = strprintf("Error: Couldn't set properties on socket for incoming connections (error %d)", WSAGetLastError());
		
		LogPrintf("%s\n", strError);
		
		return false;
	}

	// some systems don't have IPV6_V6ONLY but are always v6only; others do have the option
	// and enable it by default or not. Try to enable it, if possible.
	if (addrBind.IsIPv6())
	{
#ifdef IPV6_V6ONLY
	#ifdef WIN32
		setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&nOne, sizeof(int));
	#else // WIN32
		setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&nOne, sizeof(int));
	#endif // WIN32
#endif // IPV6_V6ONLY

#ifdef WIN32
		int nProtLevel = 10 /* PROTECTION_LEVEL_UNRESTRICTED */;
		int nParameterId = 23 /* IPV6_PROTECTION_LEVEl */;
		
		// this call is allowed to fail
		setsockopt(hListenSocket, IPPROTO_IPV6, nParameterId, (const char*)&nProtLevel, sizeof(int));
#endif // WIN32
    }

	if (::bind(hListenSocket, (struct sockaddr*)&sockaddr, len) == SOCKET_ERROR)
	{
		int nErr = WSAGetLastError();
		
		if (nErr == WSAEADDRINUSE)
		{
			strError = strprintf(ui_translate("Unable to bind to %s on this computer. DigitalNote is probably already running."), addrBind.ToString());
		}
		else
		{
			strError = strprintf(ui_translate("Unable to bind to %s on this computer (bind returned error %d, %s)"), addrBind.ToString(), nErr, strerror(nErr));
		}

		LogPrintf("%s\n", strError);
		
		return false;
	}

	LogPrintf("Bound to %s\n", addrBind.ToString());

	// Listen for incoming connections
	if (listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		strError = strprintf("Error: Listening for incoming connections failed (listen returned error %d)", WSAGetLastError());
		
		LogPrintf("%s\n", strError);
		
		return false;
	}

	vhListenSocket.push_back(hListenSocket);

	if (addrBind.IsRoutable() && fDiscover)
	{
		AddLocal(addrBind, LOCAL_BIND);
	}

	return true;
}

void static Discover(boost::thread_group& threadGroup)
{
	if (!fDiscover)
	{
		return;
	}

#ifdef WIN32
	// Get local host IP
	char pszHostName[1000] = "";

	if (gethostname(pszHostName, sizeof(pszHostName)) != SOCKET_ERROR)
	{
		std::vector<CNetAddr> vaddr;
		
		if (LookupHost(pszHostName, vaddr))
		{
			for(const CNetAddr &addr : vaddr)
			{
				AddLocal(addr, LOCAL_IF);
			}
		}
	}
#else
	// Get local host ip
	struct ifaddrs* myaddrs;

	if (getifaddrs(&myaddrs) == 0)
	{
		for (struct ifaddrs* ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next)
		{
			if (ifa->ifa_addr == NULL ||
				(ifa->ifa_flags & IFF_UP) == 0 ||
				strcmp(ifa->ifa_name, "lo") == 0 ||
				strcmp(ifa->ifa_name, "lo0") == 0
			)
			{
				continue;
			}
			
			if (ifa->ifa_addr->sa_family == AF_INET)
			{
				struct sockaddr_in* s4 = (struct sockaddr_in*)(ifa->ifa_addr);
				CNetAddr addr(s4->sin_addr);
				
				if (AddLocal(addr, LOCAL_IF))
				{
					LogPrintf("IPv4 %s: %s\n", ifa->ifa_name, addr.ToString());
				}
			}
			else if (ifa->ifa_addr->sa_family == AF_INET6)
			{
				struct sockaddr_in6* s6 = (struct sockaddr_in6*)(ifa->ifa_addr);
				CNetAddr addr(s6->sin6_addr);
				
				if (AddLocal(addr, LOCAL_IF))
				{
					LogPrintf("IPv6 %s: %s\n", ifa->ifa_name, addr.ToString());
				}
			}
		}
		
		freeifaddrs(myaddrs);
	}
#endif

}

void StartNode(boost::thread_group& threadGroup)
{
	//try to read stored banlist
	CBanDB bandb;
	banmap_t banmap;

	if (!bandb.Read(banmap))
	{
		LogPrintf("Invalid or missing banlist.dat; recreating\n");
	}

	CNode::SetBanned(banmap); //thread save setter
	CNode::SetBannedSetDirty(false); //no need to write down just read or nonexistent data
	CNode::SweepBanned(); //sweap out unused entries

	if (semOutbound == NULL)
	{
		// initialize semaphore
		int nMaxOutbound = std::min(MAX_OUTBOUND_CONNECTIONS, nMaxConnections);
		semOutbound = new CSemaphore(nMaxOutbound);
	}

	if (pnodeLocalHost == NULL)
	{
		pnodeLocalHost = new CNode(INVALID_SOCKET, CAddress(CService("127.0.0.1", 0), nLocalServices));
	}

	Discover(threadGroup);

	//
	// Start threads
	//

	if (!GetBoolArg("-dnsseed", true))
	{
		LogPrintf("DNS seeding disabled\n");
	}
	else
	{
		threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "dnsseed", &ThreadDNSAddressSeed));
	}

#ifdef USE_UPNP
	// Map ports with UPnP
	MapPort(GetBoolArg("-upnp", USE_UPNP));
#endif

	// Send and receive from sockets, accept connections
	threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "net", &ThreadSocketHandler));

	// Initiate outbound connections from -addnode
	threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "addcon", &ThreadOpenAddedConnections));

	// Initiate outbound connections
	threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "opencon", &ThreadOpenConnections));

	// Process messages
	threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "msghand", &ThreadMessageHandler));

	// Dump network addresses
	threadGroup.create_thread(boost::bind(&LoopForever<void (*)()>, "dumpaddr", &DumpData, DUMP_ADDRESSES_INTERVAL * 1000));
}

bool StopNode()
{
	LogPrintf("StopNode()\n");

	MapPort(false);
	mempool.AddTransactionsUpdated(1);

	if (semOutbound)
	{
		for (int i=0; i<MAX_OUTBOUND_CONNECTIONS; i++)
		{
			semOutbound->post();
		}
	}

	DumpData();
	MilliSleep(50);
	DumpAddresses();

	return true;
}

void RelayTransaction(const CTransaction& tx, const uint256& hash)
{
	CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

	ss.reserve(10000);
	ss << tx;

	RelayTransaction(tx, hash, ss);
}

void RelayTransaction(const CTransaction& tx, const uint256& hash, const CDataStream& ss)
{
	CInv inv(MSG_TX, hash);

	{
		LOCK(cs_mapRelay);
		
		// Expire old relay messages
		while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime())
		{
			mapRelay.erase(vRelayExpiration.front().second);
			vRelayExpiration.pop_front();
		}

		// Save original serialized message so newer versions are preserved
		mapRelay.insert(std::make_pair(inv, ss));
		vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
	}

	RelayInventory(inv);
}

void RelayTransactionLockReq(const CTransaction& tx, bool relayToAll)
{
	CInv inv(MSG_TXLOCK_REQUEST, tx.GetHash());

	//broadcast the new lock
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		if(!relayToAll && !pnode->fRelayTxes)
		{
			continue;
		}
		
		pnode->PushMessage("txlreq", tx);
	}
}

void DumpBanlist()
{
	int64_t nStart = GetTimeMillis();

	CNode::SweepBanned(); //clean unused entires (if bantime has expired)

	CBanDB bandb;
	banmap_t banmap;
	CNode::GetBanned(banmap);
	bandb.Write(banmap);

	LogPrint("net", "Flushed %d banned node ips/subnets to banlist.dat  %dms\n",
		banmap.size(),
		GetTimeMillis() - nStart
	);
}

