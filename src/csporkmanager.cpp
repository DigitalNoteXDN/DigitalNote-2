#include "compat.h"

#include <boost/lexical_cast.hpp>

#include "util.h"
#include "net.h"
#include "uint/uint256.h"
#include "cinv.h"
#include "ckey.h"
#include "cpubkey.h"
#include "cscriptid.h"
#include "csporkmessage.h"
#include "spork.h"
#include "cmnenginesigner.h"
#include "mnengine.h"
#include "mnengine_extern.h"

#include "csporkmanager.h"

CSporkManager::CSporkManager()
{
	strMainPubKey = "04d244288a8c6ebbf491443ebfa1207275d71cb009f201c118b00cf8e77641c7f1e63e330ba909842c009af375c0f5c1c7368e8d7e2066168c40ce3cb629cf212f";
	strTestPubKey = "04d244288a8c6ebbf491443ebfa1207275d71cb009f201c118b00cf8e77641c7f1e63e330ba909842c009af375c0f5c1c7368e8d7e2066168c40ce3cb629cf212f";
}

std::string CSporkManager::GetSporkNameByID(int id)
{
	if(id == SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT)	return "SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT";
	if(id == SPORK_2_INSTANTX)							return "SPORK_2_INSTANTX";
	if(id == SPORK_3_INSTANTX_BLOCK_FILTERING)			return "SPORK_3_INSTANTX_BLOCK_FILTERING";
	if(id == SPORK_5_MAX_VALUE)							return "SPORK_5_MAX_VALUE";
	if(id == SPORK_6_REPLAY_BLOCKS)						return "SPORK_6_REPLAY_BLOCKS";
	if(id == SPORK_7_MASTERNODE_SCANNING)				return "SPORK_7_MASTERNODE_SCANNING";
	if(id == SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)	return "SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT";
	if(id == SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT)		return "SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT";
	if(id == SPORK_10_MASTERNODE_PAY_UPDATED_NODES)		return "SPORK_10_MASTERNODE_PAY_UPDATED_NODES";
	if(id == SPORK_11_RESET_BUDGET)						return "SPORK_11_RESET_BUDGET";
	if(id == SPORK_12_RECONSIDER_BLOCKS)				return "SPORK_12_RECONSIDER_BLOCKS";
	if(id == SPORK_13_ENABLE_SUPERBLOCKS)				return "SPORK_13_ENABLE_SUPERBLOCKS";

	return "Unknown";
}

int CSporkManager::GetSporkIDByName(std::string strName)
{
	if(strName == "SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT")	return SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT;
	if(strName == "SPORK_2_INSTANTX")							return SPORK_2_INSTANTX;
	if(strName == "SPORK_3_INSTANTX_BLOCK_FILTERING")			return SPORK_3_INSTANTX_BLOCK_FILTERING;
	if(strName == "SPORK_5_MAX_VALUE")							return SPORK_5_MAX_VALUE;
	if(strName == "SPORK_6_REPLAY_BLOCKS")						return SPORK_6_REPLAY_BLOCKS;
	if(strName == "SPORK_7_MASTERNODE_SCANNING")				return SPORK_7_MASTERNODE_SCANNING;
	if(strName == "SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT")		return SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT;
	if(strName == "SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT")		return SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT;
	if(strName == "SPORK_10_MASTERNODE_PAY_UPDATED_NODES")		return SPORK_10_MASTERNODE_PAY_UPDATED_NODES;
	if(strName == "SPORK_11_RESET_BUDGET")						return SPORK_11_RESET_BUDGET;
	if(strName == "SPORK_12_RECONSIDER_BLOCKS")					return SPORK_12_RECONSIDER_BLOCKS;
	if(strName == "SPORK_13_ENABLE_SUPERBLOCKS")				return SPORK_13_ENABLE_SUPERBLOCKS;

	return -1;
}

bool CSporkManager::UpdateSpork(int nSporkID, int64_t nValue)
{
	CSporkMessage msg;
	
	msg.nSporkID = nSporkID;
	msg.nValue = nValue;
	msg.nTimeSigned = GetTime();

	if(Sign(msg))
	{
		Relay(msg);
		
		mapSporks[msg.GetHash()] = msg;
		mapSporksActive[nSporkID] = msg;
		
		return true;
	}

	return false;
}

bool CSporkManager::SetPrivKey(const std::string &strPrivKey)
{
	CSporkMessage msg;

	// Test signing successful, proceed
	strMasterPrivKey = strPrivKey;

	Sign(msg);

	if(CheckSignature(msg))
	{
		LogPrintf("CSporkManager::SetPrivKey - Successfully initialized as spork signer\n");
		
		return true;
	}
	else
	{
		return false;
	}
}

bool CSporkManager::CheckSignature(CSporkMessage& spork)
{
	//note: need to investigate why this is failing
	std::string strMessage = boost::lexical_cast<std::string>(spork.nSporkID) +
							boost::lexical_cast<std::string>(spork.nValue) +
							boost::lexical_cast<std::string>(spork.nTimeSigned);
	std::string strPubKey = strMainPubKey;
	CPubKey pubkey(ParseHex(strPubKey));
	std::string errorMessage = "";

	if(!mnEngineSigner.VerifyMessage(pubkey, spork.vchSig, strMessage, errorMessage))
	{
		return false;
	}

	return true;
}

bool CSporkManager::Sign(CSporkMessage& spork)
{
	std::string strMessage = boost::lexical_cast<std::string>(spork.nSporkID) +
							boost::lexical_cast<std::string>(spork.nValue) +
							boost::lexical_cast<std::string>(spork.nTimeSigned);
	CKey key2;
	CPubKey pubkey2;
	std::string errorMessage = "";

	if(!mnEngineSigner.SetKey(strMasterPrivKey, errorMessage, key2, pubkey2))
	{
		LogPrintf("CMasternodePayments::Sign - ERROR: Invalid masternodeprivkey: '%s'\n", errorMessage.c_str());
		
		return false;
	}

	if(!mnEngineSigner.SignMessage(strMessage, errorMessage, spork.vchSig, key2))
	{
		LogPrintf("CMasternodePayments::Sign - Sign message failed");
		
		return false;
	}

	if(!mnEngineSigner.VerifyMessage(pubkey2, spork.vchSig, strMessage, errorMessage))
	{
		LogPrintf("CMasternodePayments::Sign - Verify message failed");
		
		return false;
	}

	return true;
}

void CSporkManager::Relay(CSporkMessage& msg)
{
	CInv inv(MSG_SPORK, msg.GetHash());

	RelayInventory(inv);
}

