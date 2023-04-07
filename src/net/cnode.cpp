#include "compat.h"

#include "cinv.h"
#include "net/cbanentry.h"
#include "net/cnodestats.h"
#include "net/cnetmessage.h"
#include "crypto/bmw/bmw512.h"
#include "net.h"
#include "util.h"
#include "netbase.h"
#include "net/csubnet.h"
#include "net/myclosesocket.h"
#include "thread.h"
#include "enums/serialize_type.h"
#include "version.h"
#include "types/cserializedata.h"
#include "cdatastream.h"

#include "net/cnode.h"

uint64_t CNode::nTotalBytesRecv = 0;
uint64_t CNode::nTotalBytesSent = 0;
CCriticalSection CNode::cs_totalBytesRecv;
CCriticalSection CNode::cs_totalBytesSent;

banmap_t CNode::setBanned;
CCriticalSection CNode::cs_setBanned;
bool CNode::setBannedIsDirty;

CNode::CNode(SOCKET hSocketIn, CAddress addrIn, std::string addrNameIn, bool fInboundIn)
		: ssSend(SER_NETWORK, INIT_PROTO_VERSION), setAddrKnown(5000)
{
	nServices = 0;
	hSocket = hSocketIn;
	nRecvVersion = INIT_PROTO_VERSION;
	nLastSend = 0;
	nLastRecv = 0;
	nSendBytes = 0;
	nRecvBytes = 0;
	nLastSendEmpty = GetTime();
	nTimeConnected = GetTime();
	nTimeOffset = 0;
	addr = addrIn;
	addrName = addrNameIn == "" ? addr.ToStringIPPort() : addrNameIn;
	nVersion = 0;
	strSubVer = "";
	fOneShot = false;
	fClient = false; // set by version message
	fInbound = fInboundIn;
	fNetworkNode = false;
	fSuccessfullyConnected = false;
	fDisconnect = false;
	nRefCount = 0;
	nSendSize = 0;
	nSendOffset = 0;
	hashContinue = 0;
	pindexLastGetBlocksBegin = 0;
	hashLastGetBlocksEnd = 0;
	nStartingHeight = -1;
	fStartSync = false;
	fGetAddr = false;
	fRelayTxes = false; // TODO: reference this again
	hashCheckpointKnown = 0;
	setInventoryKnown.max_size(SendBufferSize() / 1000);
	nPingNonceSent = 0;
	nPingUsecStart = 0;
	nPingUsecTime = 0;
	fPingQueued = false;

	{
		LOCK(cs_nLastNodeId);
		id = nLastNodeId++;
	}

	// Be shy and don't send version until we hear
	if (hSocket != INVALID_SOCKET && !fInbound)
		PushVersion();

	GetNodeSignals().InitializeNode(GetId(), this);
}

CNode::~CNode()
{
	if (hSocket != INVALID_SOCKET)
	{
		closesocket(hSocket);
		hSocket = INVALID_SOCKET;
	}
	GetNodeSignals().FinalizeNode(GetId());
}

NodeId CNode::GetId() const {
  return id;
}

int CNode::GetRefCount()
{
	assert(nRefCount >= 0);
	return nRefCount;
}

unsigned int CNode::GetTotalRecvSize()
{
	unsigned int total = 0;
	
	for(const CNetMessage &msg : vRecvMsg)
	{
		total += msg.vRecv.size() + 24;
	}
	
	return total;
}

bool CNode::ReceiveMsgBytes(const char *pch, unsigned int nBytes)
{
	while (nBytes > 0)
	{

		// get current incomplete message, or create a new one
		if (vRecvMsg.empty() ||
			vRecvMsg.back().complete())
			vRecvMsg.push_back(CNetMessage(SER_NETWORK, nRecvVersion));

		CNetMessage& msg = vRecvMsg.back();

		// absorb network data
		int handled;
		if (!msg.in_data)
			handled = msg.readHeader(pch, nBytes);
		else
			handled = msg.readData(pch, nBytes);

		if (handled < 0)
				return false;

		pch += handled;
		nBytes -= handled;
	}

	return true;
}

void CNode::SetRecvVersion(int nVersionIn)
{
	nRecvVersion = nVersionIn;

	for(CNetMessage &msg : vRecvMsg)
	{
		msg.SetVersion(nVersionIn);
	}
}

CNode* CNode::AddRef()
{
	nRefCount++;
	return this;
}

void CNode::Release()
{
	nRefCount--;
}

void CNode::AddAddressKnown(const CAddress& addr)
{
	setAddrKnown.insert(addr);
}

void CNode::PushAddress(const CAddress& addr)
{
	// Known checking here is only to save space from duplicates.
	// SendMessages will filter it again for knowns that were added
	// after addresses were pushed.
	if (addr.IsValid() && !setAddrKnown.count(addr)) {
		if (vAddrToSend.size() >= MAX_ADDR_TO_SEND) {
			vAddrToSend[insecure_rand() % vAddrToSend.size()] = addr;
		} else {
			vAddrToSend.push_back(addr);
		}
	}
}

void CNode::AddInventoryKnown(const CInv& inv)
{
	{
		LOCK(cs_inventory);
		setInventoryKnown.insert(inv);
	}
}

void CNode::PushInventory(const CInv& inv)
{
	{
		LOCK(cs_inventory);
		if (!setInventoryKnown.count(inv))
			vInventoryToSend.push_back(inv);
	}
}

void CNode::AskFor(const CInv& inv)
{
	if (mapAskFor.size() > MAPASKFOR_MAX_SZ)
		return;
	// a peer may not have multiple non-responded queue positions for a single inv item
	if (!setAskFor.insert(inv.hash).second)
		return;

	// We're using mapAskFor as a priority queue,
	// the key is the earliest time the request can be sent
	int64_t nRequestTime;
	limitedmap<CInv, int64_t>::const_iterator it = mapAlreadyAskedFor.find(inv);
	if (it != mapAlreadyAskedFor.end())
		nRequestTime = it->second;
	else
		nRequestTime = 0;
	LogPrint("net", "askfor %s   %d (%s)\n", inv.ToString().c_str(), nRequestTime, DateTimeStrFormat("%H:%M:%S", nRequestTime/1000000).c_str());

	// Make sure not to reuse time indexes to keep things in the same order
	int64_t nNow = GetTimeMicros() - 1000000;
	static int64_t nLastTime;
	++nLastTime;
	nNow = std::max(nNow, nLastTime);
	nLastTime = nNow;

	// Each retry is 2 minutes after the last
	nRequestTime = std::max(nRequestTime + 2 * 60 * 1000000, nNow);
	if (it != mapAlreadyAskedFor.end())
		mapAlreadyAskedFor.update(it, nRequestTime);
	else
		mapAlreadyAskedFor.insert(std::make_pair(inv, nRequestTime));
	mapAskFor.insert(std::make_pair(nRequestTime, inv));
}

// TODO: Document the postcondition of this function.  Is cs_vSend locked?
void CNode::BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSend)
{
	ENTER_CRITICAL_SECTION(cs_vSend);
	assert(ssSend.size() == 0);
	ssSend << CMessageHeader(pszCommand, 0);
	LogPrint("net", "sending: %s ", pszCommand);
}

// TODO: Document the precondition of this function.  Is cs_vSend locked?
void CNode::AbortMessage() UNLOCK_FUNCTION(cs_vSend)
{
	ssSend.clear();

	LEAVE_CRITICAL_SECTION(cs_vSend);

	LogPrint("net", "(aborted)\n");
}

// TODO: Document the precondition of this function.  Is cs_vSend locked?
void CNode::EndMessage() UNLOCK_FUNCTION(cs_vSend)
{
	// The -*messagestest options are intentionally not documented in the help message,
	// since they are only used during development to debug the networking code and are
	// not intended for end-users.
	if (mapArgs.count("-dropmessagestest") && GetRand(GetArg("-dropmessagestest", 2)) == 0)
	{
		LogPrint("net", "dropmessages DROPPING SEND MESSAGE\n");
		AbortMessage();
		return;
	}

	if (ssSend.size() == 0)
		return;

	// Set the size
	unsigned int nSize = ssSend.size() - CMessageHeader::HEADER_SIZE;
	memcpy((char*)&ssSend[CMessageHeader::MESSAGE_SIZE_OFFSET], &nSize, sizeof(nSize));

	// Set the checksum
	uint256 hash = Hash_bmw512(ssSend.begin() + CMessageHeader::HEADER_SIZE, ssSend.end());
	unsigned int nChecksum = 0;
	memcpy(&nChecksum, &hash, sizeof(nChecksum));
	assert(ssSend.size () >= CMessageHeader::CHECKSUM_OFFSET + sizeof(nChecksum));
	memcpy((char*)&ssSend[CMessageHeader::CHECKSUM_OFFSET], &nChecksum, sizeof(nChecksum));

	LogPrint("net", "(%d bytes)\n", nSize);

	std::deque<CSerializeData>::iterator it = vSendMsg.insert(vSendMsg.end(), CSerializeData());
	ssSend.GetAndClear(*it);
	nSendSize += (*it).size();

	// If write queue empty, attempt "optimistic write"
	if (it == vSendMsg.begin())
		SocketSendData(this);

	LEAVE_CRITICAL_SECTION(cs_vSend);
}

void CNode::PushVersion()
{
	/// when NTP implemented, change to just nTime = GetAdjustedTime()
	int64_t nTime = (fInbound ? GetAdjustedTime() : GetTime());
	
	CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService("0.0.0.0",0)));
	CAddress addrMe = GetLocalAddress(&addr);
	
	GetRandBytes((unsigned char*)&nLocalHostNonce, sizeof(nLocalHostNonce));
	
	LogPrint("net", "send version message: version %d, blocks=%d, us=%s, them=%s, peer=%s\n", PROTOCOL_VERSION, nBestHeight, addrMe.ToString(), addrYou.ToString(), addr.ToString());
	
	PushMessage("version", PROTOCOL_VERSION, nLocalServices, nTime, addrYou, addrMe,
				nLocalHostNonce, strSubVersion, nBestHeight);
}

void CNode::PushMessage(const char* pszCommand)
{
	try
	{
		BeginMessage(pszCommand);
		EndMessage();
	}
	catch (...)
	{
		AbortMessage();
		throw;
	}
}

bool CNode::HasFulfilledRequest(std::string strRequest)
{
	for(std::string& type : vecRequestsFulfilled)
	{
		if(type == strRequest)
		{
			return true;
		}
	}

	return false;
}

void CNode::FulfilledRequest(std::string strRequest)
{
	if(HasFulfilledRequest(strRequest)) return;
	vecRequestsFulfilled.push_back(strRequest);
}

void CNode::CloseSocketDisconnect()
{
	fDisconnect = true;
	if (hSocket != INVALID_SOCKET)
	{
		LogPrint("net", "disconnecting node %s\n", addrName);
		closesocket(hSocket);
		hSocket = INVALID_SOCKET;
	}

	// in case this fails, we'll empty the recv buffer when the CNode is deleted
	TRY_LOCK(cs_vRecvMsg, lockRecv);
	if (lockRecv)
		vRecvMsg.clear();

	// if this was the sync node, we'll need a new one
	if (this == pnodeSync)
		pnodeSync = NULL;
}

void CNode::ClearBanned()
{
	LOCK(cs_setBanned);
	
	setBanned.clear();
	setBannedIsDirty = true;
}

bool CNode::IsBanned(CNetAddr ip)
{
	bool fResult = false;
	
	{
		LOCK(cs_setBanned);
		
		for (banmap_t::iterator it = setBanned.begin(); it != setBanned.end(); it++)
		{
			CSubNet subNet = (*it).first;
			CBanEntry banEntry = (*it).second;

			if(subNet.Match(ip) && GetTime() < banEntry.nBanUntil)
				fResult = true;
		}
	}
	
	return fResult;
}

bool CNode::IsBanned(CSubNet subnet)
{
	bool fResult = false;
	{
		LOCK(cs_setBanned);
		
		banmap_t::iterator i = setBanned.find(subnet);
		
		if (i != setBanned.end())
		{
			CBanEntry banEntry = (*i).second;
			
			if (GetTime() < banEntry.nBanUntil)
				fResult = true;
		}
	}
	
	return fResult;
}

void CNode::Ban(const CNetAddr& addr, const BanReason &banReason, int64_t bantimeoffset, bool sinceUnixEpoch)
{
	CSubNet subNet(addr.ToString()+(addr.IsIPv4() ? "/32" : "/128"));
	Ban(subNet, banReason, bantimeoffset, sinceUnixEpoch);
}

void CNode::Ban(const CSubNet& subNet, const BanReason &banReason, int64_t bantimeoffset, bool sinceUnixEpoch)
{
	CBanEntry banEntry(GetTime());
	banEntry.banReason = banReason;
	
	if (bantimeoffset <= 0)
	{
		bantimeoffset = GetArg("-bantime", 60*60*24); // Default 24-hour ban
		sinceUnixEpoch = false;
	}
	
	banEntry.nBanUntil = (sinceUnixEpoch ? 0 : GetTime() )+bantimeoffset;


	LOCK(cs_setBanned);
	
	if (setBanned[subNet].nBanUntil < banEntry.nBanUntil)
		setBanned[subNet] = banEntry;

	setBannedIsDirty = true;
}

bool CNode::Unban(const CNetAddr &addr)
{
	CSubNet subNet(addr.ToString()+(addr.IsIPv4() ? "/32" : "/128"));
	
	return Unban(subNet);
}

bool CNode::Unban(const CSubNet &subNet)
{
	LOCK(cs_setBanned);
	
	if (setBanned.erase(subNet))
	{
		setBannedIsDirty = true;
		
		return true;
	}
	
	return false;
}

void CNode::GetBanned(banmap_t &banMap)
{
	LOCK(cs_setBanned);
	
	banMap = setBanned; //create a thread safe copy
}

void CNode::SetBanned(const banmap_t &banMap)
{
	LOCK(cs_setBanned);

	setBanned = banMap;
	setBannedIsDirty = true;
}

bool CNode::BannedSetIsDirty()
{
	LOCK(cs_setBanned);
	
	return setBannedIsDirty;
}

void CNode::SetBannedSetDirty(bool dirty)
{
	LOCK(cs_setBanned); //reuse setBanned lock for the isDirty flag
	
	setBannedIsDirty = dirty;
}

void CNode::SweepBanned()
{
	int64_t now = GetTime();

	LOCK(cs_setBanned);
	
	banmap_t::iterator it = setBanned.begin();
	
	while(it != setBanned.end())
	{
		CBanEntry banEntry = (*it).second;
		
		if(now > banEntry.nBanUntil)
		{
			setBanned.erase(it++);
			setBannedIsDirty = true;
		}
		else
		{
			++it;
		}
	}
}

#undef X
#define X(name) stats.name = name
void CNode::copyStats(CNodeStats &stats)
{
	stats.nodeid = this->GetId();
	X(nServices);
	X(nLastSend);
	X(nLastRecv);
	X(nTimeConnected);
	X(nTimeOffset);
	X(addrName);
	X(nVersion);
	X(cleanSubVer);
	X(strSubVer);
	X(fInbound);
	X(nStartingHeight);
	X(nSendBytes);
	X(nRecvBytes);
	stats.fSyncNode = (this == pnodeSync);

	// It is common for nodes with good ping times to suddenly become lagged,
	// due to a new block arriving or other large transfer.
	// Merely reporting pingtime might fool the caller into thinking the node was still responsive,
	// since pingtime does not update until the ping is complete, which might take a while.
	// So, if a ping is taking an unusually long time in flight,
	// the caller can immediately detect that this is happening.
	int64_t nPingUsecWait = 0;
	if ((0 != nPingNonceSent) && (0 != nPingUsecStart)) {
		nPingUsecWait = GetTimeMicros() - nPingUsecStart;
	}

	// Raw ping time is in microseconds, but show it to user as whole seconds (DigitalNote users should be well used to small numbers with many decimal places by now :)
	stats.dPingTime = (((double)nPingUsecTime) / 1e6);
	stats.dPingWait = (((double)nPingUsecWait) / 1e6);

	// Leave string empty if addrLocal invalid (not filled in yet)
	stats.addrLocal = addrLocal.IsValid() ? addrLocal.ToString() : "";
}
#undef X

void CNode::RecordBytesRecv(uint64_t bytes)
{
	LOCK(cs_totalBytesRecv);
	
	nTotalBytesRecv += bytes;
}

void CNode::RecordBytesSent(uint64_t bytes)
{
	LOCK(cs_totalBytesSent);
	
	nTotalBytesSent += bytes;
}

uint64_t CNode::GetTotalBytesRecv()
{
	LOCK(cs_totalBytesRecv);
	
	return nTotalBytesRecv;
}

uint64_t CNode::GetTotalBytesSent()
{
	LOCK(cs_totalBytesSent);
	
	return nTotalBytesSent;
}

