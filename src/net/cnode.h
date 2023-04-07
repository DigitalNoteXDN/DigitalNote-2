#ifndef CNODE_H
#define CNODE_H

#include "mruset.h"
#include "net/secmsgnode.h"
#include "net/banreason.h"
#include "net/cnetmessage.h"
#include "caddress.h"
#include "uint/uint256.h"
#include "thread/csemaphoregrant.h"
#include "types/nodeid.h"
#include "types/banmap_t.h"

#if defined(__clang__) && defined(MAC_OSX)
	#include "cinv.h"
#endif // defined(__clang__) && defined(MAC_OSX)

class CSubNet;
class CBlockIndex;
class CNodeStats;
class CInv;
struct CNodeSignals;

/** Information about a peer */
class CNode
{
private:
	// Network usage totals
	static CCriticalSection cs_totalBytesRecv;
	static CCriticalSection cs_totalBytesSent;
	static uint64_t nTotalBytesRecv;
	static uint64_t nTotalBytesSent;

protected:
	// Denial-of-service detection/prevention
	// Key is IP address, value is banned-until-time
	static banmap_t setBanned;
	static CCriticalSection cs_setBanned;
	static bool setBannedIsDirty;

	std::vector<std::string> vecRequestsFulfilled; //keep track of what client has asked for

public:
	// socket
	uint64_t nServices;
	SOCKET hSocket;
	CDataStream ssSend;
	size_t nSendSize; // total size of all vSendMsg entries
	size_t nSendOffset; // offset inside the first vSendMsg already sent
	uint64_t nSendBytes;
	std::deque<CSerializeData> vSendMsg;
	CCriticalSection cs_vSend;

	std::deque<CInv> vRecvGetData;
	std::deque<CNetMessage> vRecvMsg;
	CCriticalSection cs_vRecvMsg;
	uint64_t nRecvBytes;
	int nRecvVersion;

	int64_t nLastSend;
	int64_t nLastRecv;
	int64_t nLastSendEmpty;
	int64_t nTimeConnected;
	int64_t nTimeOffset;
	CAddress addr;
	std::string addrName;
	CService addrLocal;
	int nVersion;
	// strSubVer is whatever byte array we read from the wire. However, this field is intended
	// to be printed out, displayed to humans in various forms and so on. So we sanitize it and
	// store the sanitized version in cleanSubVer. The original should be used when dealing with
	// the network or wire types and the cleaned string used when displayed or logged.
	std::string strSubVer, cleanSubVer;
	bool fOneShot;
	bool fClient;
	bool fInbound;
	bool fNetworkNode;
	bool fSuccessfullyConnected;
	bool fDisconnect;
	// We use fRelayTxes for two purposes -
	// a) it allows us to not relay tx invs before receiving the peer's version message
	// b) the peer may tell us in their version message that we should not relay tx invs
	//    until they have initialized their bloom filter.
	bool fRelayTxes;
	bool fMNengineMaster;
	CSemaphoreGrant grantOutbound;
	int nRefCount;
	NodeId id;

	uint256 hashContinue;
	CBlockIndex* pindexLastGetBlocksBegin;
	uint256 hashLastGetBlocksEnd;
	int nStartingHeight;
	bool fStartSync;

	// flood relay
	std::vector<CAddress> vAddrToSend;
	mruset<CAddress> setAddrKnown;
	bool fGetAddr;
	std::set<uint256> setKnown;
	uint256 hashCheckpointKnown; // ppcoin: known sent sync-checkpoint

	// inventory based relay
	mruset<CInv> setInventoryKnown;
	std::vector<CInv> vInventoryToSend;
	CCriticalSection cs_inventory;
	std::set<uint256> setAskFor;
	std::multimap<int64_t, CInv> mapAskFor;

	// D-Note relay
	SecMsgNode smsgData;

	// Ping time measurement:
	// The pong reply we're expecting, or 0 if no pong expected.
	uint64_t nPingNonceSent;
	// Time (in usec) the last ping was sent, or 0 if no ping was ever sent.
	int64_t nPingUsecStart;
	// Last measured round-trip time.
	int64_t nPingUsecTime;
	// Whether a ping is requested.
	bool fPingQueued;

	CNode(SOCKET hSocketIn, CAddress addrIn, std::string addrNameIn = "", bool fInboundIn=false);
	~CNode();

	NodeId GetId() const;
	int GetRefCount();
	unsigned int GetTotalRecvSize();
	bool ReceiveMsgBytes(const char *pch, unsigned int nBytes);
	void SetRecvVersion(int nVersionIn);
	CNode* AddRef();
	void Release();
	void AddAddressKnown(const CAddress& addr);
	void PushAddress(const CAddress& addr);
	void AddInventoryKnown(const CInv& inv);
	void PushInventory(const CInv& inv);
	void AskFor(const CInv& inv);
	void BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSend);
	void AbortMessage() UNLOCK_FUNCTION(cs_vSend);
	void EndMessage() UNLOCK_FUNCTION(cs_vSend);
	void PushVersion();
	void PushMessage(const char* pszCommand);

	template<typename T1>
	void PushMessage(const char* pszCommand, const T1& a1)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}

	template<typename T1, typename T2>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}

	template<typename T1, typename T2, typename T3>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}

	template<typename T1, typename T2, typename T3, typename T4>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}

	template<typename T1, typename T2, typename T3, typename T4, typename T5>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}

	template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}

	template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}

	template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}

	template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8, const T9& a9)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8 << a9;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}

	template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9, typename T10>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8, const T9& a9, const T10& a10)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8 << a9 << a10;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}
	template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9, typename T10, typename T11>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8, const T9& a9, const T10& a10, const T11& a11)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8 << a9 << a10 << a11;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
		   throw;
		}
	}

	template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9, typename T10, typename T11, typename T12>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8, const T9& a9, const T10& a10, const T11& a11, const T12& a12)
	{
		try
		{
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8 << a9 << a10 << a11 << a12;
			EndMessage();
		}
		catch (...)
		{
			AbortMessage();
			throw;
		}
	}

	bool HasFulfilledRequest(std::string strRequest);
	void FulfilledRequest(std::string strRequest);

	// ? Deleted ?
	//bool IsSubscribed(unsigned int nChannel);
	//void Subscribe(unsigned int nChannel, unsigned int nHops=0);
	//void CancelSubscribe(unsigned int nChannel);

	void CloseSocketDisconnect();

	// Denial-of-service detection/prevention
	// The idea is to detect peers that are behaving
	// badly and disconnect/ban them, but do it in a
	// one-coding-mistake-won't-shatter-the-entire-network
	// way.
	// IMPORTANT:  There should be nothing I can give a
	// node that it will forward on that will make that
	// node's peers drop it. If there is, an attacker
	// can isolate a node and/or try to split the network.
	// Dropping a node for sending stuff that is invalid
	// now but might be valid in a later version is also
	// dangerous, because it can cause a network split
	// between nodes running old code and nodes running
	// new code.
	static void ClearBanned(); // needed for unit testing
	static bool IsBanned(CNetAddr ip);
	static bool IsBanned(CSubNet subnet);
	static void Ban(const CNetAddr &ip, const BanReason &banReason, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
	static void Ban(const CSubNet &subNet, const BanReason &banReason, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
	static bool Unban(const CNetAddr &ip);
	static bool Unban(const CSubNet &ip);
	static void GetBanned(banmap_t &banmap);
	static void SetBanned(const banmap_t &banmap);

	//!check is the banlist has unwritten changes
	static bool BannedSetIsDirty();
	//!set the "dirty" flag for the banlist
	static void SetBannedSetDirty(bool dirty=true);
	//!clean unused entires (if bantime has expired)
	static void SweepBanned();

	void copyStats(CNodeStats &stats);

	// Network stats
	static void RecordBytesRecv(uint64_t bytes);
	static void RecordBytesSent(uint64_t bytes);

	static uint64_t GetTotalBytesRecv();
	static uint64_t GetTotalBytesSent();
};

#endif // CNODE_H
