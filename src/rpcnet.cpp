#include "compat.h"

#include "enums/rpcerrorcode.h"
#include "rpcserver.h"
#include "calert.h"
#include "cnodestatestats.h"
#include "util.h"
#include "json/json_spirit_value.h"
#include "net/cnode.h"
#include "net/cnodestats.h"
#include "net/cbanentry.h"
#include "net.h"
#include "main.h"
#include "ckey.h"
#include "hash.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "netbase.h"
#include "net/csubnet.h"
#include "thread.h"
#include "ui_interface.h"
#include "rpcprotocol.h"

typedef std::list<std::pair<std::string, std::vector<CService>>> addresslist_t;

json_spirit::Value getconnectioncount(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error(
			"getconnectioncount\n"
			"Returns the number of connections to other nodes."
		);
	}

	LOCK(cs_vNodes);

	return (int)vNodes.size();
}

json_spirit::Value ping(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error(
			"ping\n"
			"Requests that a ping be sent to all other nodes, to measure ping time.\n"
			"Results provided in getpeerinfo, pingtime and pingwait fields are decimal seconds.\n"
			"Ping command is handled in queue with all other commands, so it measures processing backlog, not just network ping."
		);
	}

	// Request that each node send a ping during next message processing pass
	LOCK(cs_vNodes);

	for(CNode* pNode : vNodes)
	{
		pNode->fPingQueued = true;
	}

	return json_spirit::Value::null;
}

static void CopyNodeStats(std::vector<CNodeStats>& vstats)
{
	vstats.clear();

	LOCK(cs_vNodes);

	vstats.reserve(vNodes.size());

	for(CNode* pnode : vNodes)
	{
		CNodeStats stats;
		
		pnode->copyStats(stats);
		vstats.push_back(stats);
	}
}

json_spirit::Value getpeerinfo(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error(
			"getpeerinfo\n"
			"Returns data about each connected network node."
		);
	}

	std::vector<CNodeStats> vstats;
	CopyNodeStats(vstats);
	json_spirit::Array ret;

	for(const CNodeStats& stats : vstats)
	{
		json_spirit::Object obj;
		CNodeStateStats statestats;
		bool fStateStats = GetNodeStateStats(stats.nodeid, statestats);
		
		obj.push_back(json_spirit::Pair("addr", stats.addrName));
		
		if (!(stats.addrLocal.empty()))
		{
			obj.push_back(json_spirit::Pair("addrlocal", stats.addrLocal));
		}
		
		obj.push_back(json_spirit::Pair("services", strprintf("%08x", stats.nServices)));
		obj.push_back(json_spirit::Pair("lastsend", (int64_t)stats.nLastSend));
		obj.push_back(json_spirit::Pair("lastrecv", (int64_t)stats.nLastRecv));
		obj.push_back(json_spirit::Pair("bytessent", (int64_t)stats.nSendBytes));
		obj.push_back(json_spirit::Pair("bytesrecv", (int64_t)stats.nRecvBytes));
		obj.push_back(json_spirit::Pair("conntime", (int64_t)stats.nTimeConnected));
		obj.push_back(json_spirit::Pair("timeoffset", stats.nTimeOffset));
		obj.push_back(json_spirit::Pair("pingtime", stats.dPingTime));
		
		if (stats.dPingWait > 0.0)
		{
			obj.push_back(json_spirit::Pair("pingwait", stats.dPingWait));
		}
		
		obj.push_back(json_spirit::Pair("version", stats.nVersion));
		obj.push_back(json_spirit::Pair("subver", stats.strSubVer));
		obj.push_back(json_spirit::Pair("inbound", stats.fInbound));
		obj.push_back(json_spirit::Pair("startingheight", stats.nStartingHeight));
		
		if (fStateStats)
		{
			obj.push_back(json_spirit::Pair("banscore", statestats.nMisbehavior));
		}
		
		obj.push_back(json_spirit::Pair("syncnode", stats.fSyncNode));

		ret.push_back(obj);
	}

	return ret;
}

json_spirit::Value addnode(const json_spirit::Array& params, bool fHelp)
{
	std::string strCommand;

	if (params.size() == 2)
	{
		strCommand = params[1].get_str();
	}

	if (fHelp || params.size() != 2 ||
		(
			strCommand != "onetry" &&
			strCommand != "add" &&
			strCommand != "remove"
		)
	)
	{
		throw std::runtime_error(
			"addnode <node> <add|remove|onetry>\n"
			"Attempts add or remove <node> from the addnode list or try a connection to <node> once."
		);
	}

	std::string strNode = params[0].get_str();

	if (strCommand == "onetry")
	{
		CAddress addr;
		ConnectNode(addr, strNode.c_str());
		
		return json_spirit::Value::null;
	}

	LOCK(cs_vAddedNodes);

	std::vector<std::string>::iterator it = vAddedNodes.begin();

	for(; it != vAddedNodes.end(); it++)
	{
		if (strNode == *it)
		{
			break;
		}
	}

	if (strCommand == "add")
	{
		if (it != vAddedNodes.end())
		{
			throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: Node already added");
		}
		
		vAddedNodes.push_back(strNode);
	}
	else if(strCommand == "remove")
	{
		if (it == vAddedNodes.end())
		{
			throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
		}
		
		vAddedNodes.erase(it);
	}

	return json_spirit::Value::null;
}

json_spirit::Value getaddednodeinfo(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() < 1 || params.size() > 2)
	{
		throw std::runtime_error(
			"getaddednodeinfo <dns> [node]\n"
			"Returns information about the given added node, or all added nodes\n"
			"(note that onetry addnodes are not listed here)\n"
			"If dns is false, only a list of added nodes will be provided,\n"
			"otherwise connected information will also be available."
		);
	}

	bool fDns = params[0].get_bool();
	std::list<std::string> laddedNodes(0);

	if (params.size() == 1)
	{
		LOCK(cs_vAddedNodes);
		
		for(std::string& strAddNode : vAddedNodes)
		{
			laddedNodes.push_back(strAddNode);
		}
	}
	else
	{
		std::string strNode = params[1].get_str();
		
		LOCK(cs_vAddedNodes);
		
		for(std::string& strAddNode : vAddedNodes)
		{
			if (strAddNode == strNode)
			{
				laddedNodes.push_back(strAddNode);
				
				break;
			}
		}

		if (laddedNodes.size() == 0)
		{
			throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
		}
	}

	if (!fDns)
	{
		json_spirit::Object ret;
		
		for(std::string& strAddNode : laddedNodes)
		{
			ret.push_back(json_spirit::Pair("addednode", strAddNode));
		}
		
		return ret;
	}

	json_spirit::Array ret;
	addresslist_t laddedAddreses(0);

	for(std::string& strAddNode : laddedNodes)
	{
		std::vector<CService> vservNode(0);
		
		if(Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), fNameLookup, 0))
		{
			laddedAddreses.push_back(std::make_pair(strAddNode, vservNode));
		}
		else
		{
			json_spirit::Object obj;
			json_spirit::Array addresses;
			
			obj.push_back(json_spirit::Pair("addednode", strAddNode));
			obj.push_back(json_spirit::Pair("connected", false));
			obj.push_back(json_spirit::Pair("addresses", addresses));
		}
	}

	LOCK(cs_vNodes);

	for (addresslist_t::iterator it = laddedAddreses.begin(); it != laddedAddreses.end(); it++)
	{
		json_spirit::Object obj;
		obj.push_back(json_spirit::Pair("addednode", it->first));

		json_spirit::Array addresses;
		bool fConnected = false;
		
		for(CService& addrNode : it->second)
		{
			bool fFound = false;
			json_spirit::Object node;
			node.push_back(json_spirit::Pair("address", addrNode.ToString()));
			for(CNode* pnode : vNodes)
			{
				if (pnode->addr == addrNode)
				{
					fFound = true;
					fConnected = true;
					
					node.push_back(json_spirit::Pair("connected", pnode->fInbound ? "inbound" : "outbound"));
					
					break;
				}
			}
			
			if (!fFound)
			{
				node.push_back(json_spirit::Pair("connected", "false"));
			}
			
			addresses.push_back(node);
		}
		
		obj.push_back(json_spirit::Pair("connected", fConnected));
		obj.push_back(json_spirit::Pair("addresses", addresses));
		
		ret.push_back(obj);
	}

	return ret;
}

// ppcoin: send alert.  
// There is a known deadlock situation with ThreadMessageHandler
// ThreadMessageHandler: holds cs_vSend and acquiring cs_main in SendMessages()
// ThreadRPCServer: holds cs_main and acquiring cs_vSend in alert.RelayTo()/PushMessage()/BeginMessage()
json_spirit::Value sendalert(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() < 6)
	{
		throw std::runtime_error(
			"sendalert <message> <privatekey> <minver> <maxver> <priority> <id> [cancelupto]\n"
			"<message> is the alert text message\n"
			"<privatekey> is hex string of alert master private key\n"
			"<minver> is the minimum applicable internal client version\n"
			"<maxver> is the maximum applicable internal client version\n"
			"<priority> is integer priority number\n"
			"<id> is the alert id\n"
			"[cancelupto] cancels all alert id's up to this number\n"
			"Returns true or false."
		);
	}

	CAlert alert;
	CKey key;

	alert.strStatusBar = params[0].get_str();
	alert.nMinVer = params[2].get_int();
	alert.nMaxVer = params[3].get_int();
	alert.nPriority = params[4].get_int();
	alert.nID = params[5].get_int();

	if (params.size() > 6)
	{
		alert.nCancel = params[6].get_int();
	}

	alert.nVersion = PROTOCOL_VERSION;
	alert.nRelayUntil = GetAdjustedTime() + 365*24*60*60;
	alert.nExpiration = GetAdjustedTime() + 365*24*60*60;

	CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);

	sMsg << (CUnsignedAlert)alert;

	alert.vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());

	std::vector<unsigned char> vchPrivKey = ParseHex(params[1].get_str());

	key.SetPrivKey(CPrivKey(vchPrivKey.begin(), vchPrivKey.end()), false); // if key is not correct openssl may crash

	if (!key.Sign(Hash(alert.vchMsg.begin(), alert.vchMsg.end()), alert.vchSig))
	{
		throw std::runtime_error("Unable to sign alert, check private key?\n");  
	}

	if(!alert.ProcessAlert())
	{
		throw std::runtime_error("Failed to process alert.\n");
	}

	// Relay alert
	{
		LOCK(cs_vNodes);
		
		for(CNode* pnode : vNodes)
		{
			alert.RelayTo(pnode);
		}
	}

	json_spirit::Object result;
	
	result.push_back(json_spirit::Pair("strStatusBar", alert.strStatusBar));
	result.push_back(json_spirit::Pair("nVersion", alert.nVersion));
	result.push_back(json_spirit::Pair("nMinVer", alert.nMinVer));
	result.push_back(json_spirit::Pair("nMaxVer", alert.nMaxVer));
	result.push_back(json_spirit::Pair("nPriority", alert.nPriority));
	result.push_back(json_spirit::Pair("nID", alert.nID));

	if (alert.nCancel > 0)
	{
		result.push_back(json_spirit::Pair("nCancel", alert.nCancel));
	}

	return result;
}

json_spirit::Value getnettotals(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() > 0)
	{
		throw std::runtime_error(
			"getnettotals\n"
			"Returns information about network traffic, including bytes in, bytes out,\n"
			"and current time."
		);
	}

	json_spirit::Object obj;

	obj.push_back(json_spirit::Pair("totalbytesrecv", CNode::GetTotalBytesRecv()));
	obj.push_back(json_spirit::Pair("totalbytessent", CNode::GetTotalBytesSent()));
	obj.push_back(json_spirit::Pair("timemillis", GetTimeMillis()));

	return obj;
}

json_spirit::Value setban(const json_spirit::Array& params, bool fHelp)
{
	std::string strCommand;
	if (params.size() >= 2)
	{
		strCommand = params[1].get_str();
	}

	if (fHelp || params.size() < 2 ||
		(
			strCommand != "add" &&
			strCommand != "remove"
		)
	)
	{
		throw std::runtime_error(
			"setban \"ip(/netmask)\" \"add|remove\" (bantime) (absolute)\n"
			"\nAttempts add or remove a IP/Subnet from the banned list.\n"
			"\nArguments:\n"
			"1. \"ip(/netmask)\" (string, required) The IP/Subnet (see getpeerinfo for nodes ip) with a optional netmask (default is /32 = single ip)\n"
			"2. \"command\"      (string, required) 'add' to add a IP/Subnet to the list, 'remove' to remove a IP/Subnet from the list\n"
			"3. \"bantime\"      (numeric, optional) time in seconds how long (or until when if [absolute] is set) the ip is banned (0 or empty means using the default time of 24h which can also be overwritten by the -bantime startup argument)\n"
			"4. \"absolute\"     (boolean, optional) If set, the bantime must be a absolute timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
			"\nExamples:\n"
			+ HelpExampleCli("setban", "\"192.168.0.6\" \"add\" 86400")
			+ HelpExampleCli("setban", "\"192.168.0.0/24\" \"add\"")
			+ HelpExampleRpc("setban", "\"192.168.0.6\", \"add\" 86400")
		);
	}

	CSubNet subNet;
	CNetAddr netAddr;
	bool isSubnet = false;

	if (params[0].get_str().find("/") != std::string::npos)
	{
		isSubnet = true;
	}

	if (!isSubnet)
	{
		netAddr = CNetAddr(params[0].get_str());
	}
	else
	{
		subNet = CSubNet(params[0].get_str());
	}

	if (! (isSubnet ? subNet.IsValid() : netAddr.IsValid()) )
	{
		throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: Invalid IP/Subnet");
	}

	if (strCommand == "add")
	{
		if (isSubnet ? CNode::IsBanned(subNet) : CNode::IsBanned(netAddr))
		{
			throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: IP/Subnet already banned");
		}
		
		int64_t banTime = 0; //use standard bantime if not specified
		if (params.size() >= 3 && !params[2].is_null())
		{
			banTime = params[2].get_int64();
		}
		
		bool absolute = false;
		if (params.size() == 4)
		{
			absolute = params[3].get_bool();
		}
		
		isSubnet ? CNode::Ban(subNet, BanReasonManuallyAdded, banTime, absolute) : CNode::Ban(netAddr, BanReasonManuallyAdded, banTime, absolute);

		//disconnect possible nodes
		while(CNode *bannedNode = (isSubnet ? FindNode(subNet) : FindNode(netAddr)))
		{
			bannedNode->CloseSocketDisconnect();
		}
	}
	else if(strCommand == "remove")
	{
		if (!(isSubnet ? CNode::Unban(subNet) : CNode::Unban(netAddr)))
		{
			throw JSONRPCError(RPC_MISC_ERROR, "Error: Unban failed");
		}
	}

	DumpBanlist(); //store banlist to disk
	uiInterface.BannedListChanged();

	return json_spirit::Value::null;
}

json_spirit::Value listbanned(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error(
			"listbanned\n"
			"\nList all banned IPs/Subnets.\n"
			"\nExamples:\n"
			+ HelpExampleCli("listbanned", "")
			+ HelpExampleRpc("listbanned", "")
		);
	}

	banmap_t banMap;
	CNode::GetBanned(banMap);

	json_spirit::Array bannedAddresses;
	for (banmap_t::iterator it = banMap.begin(); it != banMap.end(); it++)
	{
		CBanEntry banEntry = (*it).second;
		json_spirit::Object rec;
		
		rec.push_back(json_spirit::Pair("address", (*it).first.ToString()));
		rec.push_back(json_spirit::Pair("banned_until", banEntry.nBanUntil));
		rec.push_back(json_spirit::Pair("ban_created", banEntry.nCreateTime));
		rec.push_back(json_spirit::Pair("ban_reason", banEntry.banReasonToString()));

		bannedAddresses.push_back(rec);
	}

	return bannedAddresses;
}

json_spirit::Value clearbanned(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error(
			"clearbanned\n"
			"\nClear all banned IPs.\n"
			"\nExamples:\n"
			+ HelpExampleCli("clearbanned", "")
			+ HelpExampleRpc("clearbanned", "")
		);
	}

	CNode::ClearBanned();
	DumpBanlist(); //store banlist to disk
	uiInterface.BannedListChanged();

	return json_spirit::Value::null;
}

