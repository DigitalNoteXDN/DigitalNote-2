#ifndef NET_H
#define NET_H

#include <vector>
#include <list>
#include <deque>
#include <stdint.h>
#include <boost/signals2/signal.hpp>

#include "limitedmap.h"
#include "serialize/base.h"
#include "net/cnode.h"

class CAddrMan;
class CBlockIndex;
class CTransaction;
class CNode;
class CBanEntry;
class CSubNet;
class CInv;
class CDataStream;
class CNetAddr;
class CService;
class CAddress;
class uint256;

namespace boost
{
    class thread_group;
}

enum
{
    LOCAL_NONE,   // unknown
    LOCAL_IF,     // address a local interface listens on
    LOCAL_BIND,   // address explicit bound to
    LOCAL_UPNP,   // address reported by UPnP
    LOCAL_MANUAL, // address explicitly specified (-externalip=)

    LOCAL_MAX
};

enum
{
    MSG_TX = 1,
    MSG_BLOCK,
    // Nodes may always request a MSG_FILTERED_BLOCK in a getdata, however,
    // MSG_FILTERED_BLOCK should not appear in any invs except as a part of getdata.
    MSG_FILTERED_BLOCK,
    MSG_TXLOCK_REQUEST,
    MSG_TXLOCK_VOTE,
    MSG_SPORK,
    MSG_MASTERNODE_WINNER,
    MSG_MASTERNODE_SCANNING_ERROR,
    MSG_DSTX
};



struct LocalServiceInfo
{
    int nScore;
    int nPort;
};

// Signals for message handling
struct CNodeSignals
{
    boost::signals2::signal<int ()> GetHeight;
    boost::signals2::signal<bool (CNode*)> ProcessMessages;
    boost::signals2::signal<bool (CNode*, bool)> SendMessages;
    boost::signals2::signal<void (NodeId, const CNode*)> InitializeNode;
    boost::signals2::signal<void (NodeId)> FinalizeNode;
};

/** Time between pings automatically sent out for latency probing and keepalive (in seconds). */
static const int PING_INTERVAL = 1 * 60;
/** Time after which to disconnect, after waiting for a ping response (or inactivity). */
static const int TIMEOUT_INTERVAL = 30 * 60;
/** Time between cycles to check for idle nodes, force disconnect (seconds) **/ 
static const int IDLE_TIMEOUT = 15 * 60;
/** Time between cycles to check for idle nodes, force disconnect (seconds) **/ 
static const int DATA_TIMEOUT = 30 * 60;
/** Maximum length of strSubVer in `version` message */
static const unsigned int MAX_SUBVERSION_LENGTH = 256;
/** The maximum number of entries in an 'inv' protocol message */
static const unsigned int MAX_INV_SZ = 50000;
/** The maximum number of entries in mapAskFor */
static const size_t MAPASKFOR_MAX_SZ = MAX_INV_SZ;
/** The maximum number of entries in setAskFor (larger due to getdata latency)*/
static const size_t SETASKFOR_MAX_SZ = 2 * MAX_INV_SZ;
/** The maximum number of new addresses to accumulate before announcing. */
static const unsigned int MAX_ADDR_TO_SEND = 1000;

extern int nBestHeight;
extern bool fDiscover;
extern uint64_t nLocalServices;
extern uint64_t nLocalHostNonce;
extern CAddrMan addrman;
extern int nMaxConnections;
extern std::vector<CNode*> vNodes;
extern CCriticalSection cs_vNodes;
extern std::map<CInv, CDataStream> mapRelay;
extern std::deque<std::pair<int64_t, CInv> > vRelayExpiration;
extern CCriticalSection cs_mapRelay;
extern limitedmap<CInv, int64_t> mapAlreadyAskedFor;
extern std::vector<std::string> vAddedNodes;
extern CCriticalSection cs_vAddedNodes;
extern NodeId nLastNodeId;
extern CCriticalSection cs_nLastNodeId;
extern NodeId nLastNodeId;
extern CCriticalSection cs_mapLocalHost;
extern std::map<CNetAddr, LocalServiceInfo> mapLocalHost;
extern CNode* pnodeSync;
extern std::vector<SOCKET> vhListenSocket;
extern std::list<CNode*> vNodesDisconnected;
extern CSemaphore *semOutbound;
extern CNode* pnodeLocalHost;

/** Subversion as sent to the P2P network in `version` messages */
extern std::string strSubVersion;

bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound = NULL,
		const char *strDest = NULL, bool fOneShot = false);
unsigned int ReceiveFloodSize();
unsigned int SendBufferSize();
void AddOneShot(std::string strDest);
bool RecvLine(SOCKET hSocket, std::string& strLine);
void AddressCurrentlyConnected(const CService& addr);
CNode* FindNode(const CNetAddr& ip);
CNode* FindNode(const CSubNet& subNet);
CNode* FindNode(const std::string &addrName);
CNode* FindNode(const CService& ip);
CNode* ConnectNode(CAddress addrConnect, const char *strDest = NULL, bool mnEngineMaster=false);
bool CheckNode(CAddress addrConnect);
void MapPort(bool fUseUPnP);
unsigned short GetListenPort();
bool BindListenPort(const CService &bindAddr, std::string& strError=REF(std::string()));
void StartNode(boost::thread_group& threadGroup);
bool StopNode();
void SocketSendData(CNode *pnode);
CNodeSignals& GetNodeSignals();
bool IsPeerAddrLocalGood(CNode *pnode);
void SetLimited(enum Network net, bool fLimited = true);
bool IsLimited(enum Network net);
bool IsLimited(const CNetAddr& addr);
bool AddLocal(const CService& addr, int nScore = LOCAL_NONE);
bool AddLocal(const CNetAddr& addr, int nScore = LOCAL_NONE);
bool SeenLocal(const CService& addr);
bool IsLocal(const CService& addr);
bool GetLocal(CService &addr, const CNetAddr *paddrPeer = NULL);
bool IsReachable(const CNetAddr &addr);
void SetReachable(enum Network net, bool fFlag = true);
CAddress GetLocalAddress(const CNetAddr *paddrPeer = NULL);
void RelayInventory(const CInv& inv);
void RelayTransaction(const CTransaction& tx, const uint256& hash);
void RelayTransaction(const CTransaction& tx, const uint256& hash, const CDataStream& ss);
void RelayTransactionLockReq(const CTransaction& tx, bool relayToAll=false);
void DumpBanlist();

#endif // NET_H
