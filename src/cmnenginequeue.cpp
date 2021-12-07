#include <boost/lexical_cast.hpp>

#include "util.h"
#include "net.h"
#include "ckey.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "masternode_extern.h"
#include "cmnenginesigner.h"
#include "mnengine.h"
#include "mnengine_extern.h"
#include "thread.h"

#include "cmnenginequeue.h"

CMNengineQueue::CMNengineQueue()
{
	vin = CTxIn();
	time = 0;
	vchSig.clear();
	ready = false;
}

bool CMNengineQueue::GetAddress(CService &addr)
{
	CMasternode* pmn = mnodeman.Find(vin);

	if(pmn != NULL)
	{
		addr = pmn->addr;
		
		return true;
	}

	return false;
}

/// Get the protocol version
bool CMNengineQueue::GetProtocolVersion(int &protocolVersion)
{
	CMasternode* pmn = mnodeman.Find(vin);

	if(pmn != NULL)
	{
		protocolVersion = pmn->protocolVersion;
		
		return true;
	}

	return false;
}

/** Sign this MNengine transaction
 *  \return true if all conditions are met:
 *     1) we have an active Masternode,
 *     2) we have a valid Masternode private key,
 *     3) we signed the message successfully, and
 *     4) we verified the message successfully
 */
bool CMNengineQueue::Sign()
{
	if(!fMasterNode)
	{
		return false;
	}

	std::string strMessage = vin.ToString() + boost::lexical_cast<std::string>(time) + boost::lexical_cast<std::string>(ready);

	CKey key2;
	CPubKey pubkey2;
	std::string errorMessage = "";

	if(!mnEngineSigner.SetKey(strMasterNodePrivKey, errorMessage, key2, pubkey2))
	{
		LogPrintf("CMNengineQueue():Relay - ERROR: Invalid Masternodeprivkey: '%s'\n", errorMessage);
		LogPrintf("CMNengineQueue():Relay - FORCE BYPASS - SetKey checks!!!\n");
		
		return false;
	}

	if(!mnEngineSigner.SignMessage(strMessage, errorMessage, vchSig, key2))
	{
		LogPrintf("CMNengineQueue():Relay - Sign message failed\n");
		LogPrintf("CMNengineQueue():Relay - FORCE BYPASS - SignMessage checks!!!\n");
		
		return false;
	}

	if(!mnEngineSigner.VerifyMessage(pubkey2, vchSig, strMessage, errorMessage))
	{
		LogPrintf("CMNengineQueue():Relay - Verify message failed\n");
		LogPrintf("CMNengineQueue():Relay - FORCE BYPASS - VerifyMessage checks!!!\n");
		
		return false;
	}

	return true;
}

bool CMNengineQueue::Relay()
{
	LOCK(cs_vNodes);

	for(CNode* pnode : vNodes)
	{
		// always relay to everyone
		pnode->PushMessage("dsq", (*this));
	}

	return true;
}

/// Is this MNengine expired?
bool CMNengineQueue::IsExpired()
{
	return (GetTime() - time) > MNengine_QUEUE_TIMEOUT;// 120 seconds
}

/// Check if we have a valid Masternode address
bool CMNengineQueue::CheckSignature()
{
	CMasternode* pmn = mnodeman.Find(vin);

	if(pmn != NULL)
	{
		std::string strMessage = vin.ToString() + boost::lexical_cast<std::string>(time) + boost::lexical_cast<std::string>(ready);
		std::string errorMessage = "";
		
		if(!mnEngineSigner.VerifyMessage(pmn->pubkey2, vchSig, strMessage, errorMessage))
		{
			LogPrintf("CMNengineQueue::CheckSignature() - WARNING - Could not verify masternode address signature %s \n", vin.ToString().c_str());
			
			return error("CMNengineQueue::CheckSignature() - Got bad Masternode address signature %s \n", vin.ToString().c_str());
		}

		return true;
	}

	return false;
}

unsigned int CMNengineQueue::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	const bool fGetSize = true;
	const bool fWrite = false;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	s.nType = nType;
	s.nVersion = nVersion;

	READWRITE(vin);
	READWRITE(time);
	READWRITE(ready);
	READWRITE(vchSig);

	return nSerSize;
}

template<typename Stream>
void CMNengineQueue::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */

	READWRITE(vin);
	READWRITE(time);
	READWRITE(ready);
	READWRITE(vchSig);
}

template<typename Stream>
void CMNengineQueue::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */

	READWRITE(vin);
	READWRITE(time);
	READWRITE(ready);
	READWRITE(vchSig);
}

template void CMNengineQueue::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CMNengineQueue::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

