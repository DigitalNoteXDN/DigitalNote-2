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
	// Legacy spork pubkey -- inherited from earlier XDN releases.
	// Corresponds to XDN mainnet address dJzowBy1WUE6bax6rMhgaiBg3tGMWk9zri.
	// The private key for this pubkey is not known to be held by any
	// active project member.  Retained here so that v2.0.0.7+ nodes
	// continue to accept sporks signed with the legacy key, if any
	// such sporks ever existed or are recovered, but the project does
	// not rely on this key for operational spork broadcasts.
	strMainPubKey = "04d244288a8c6ebbf491443ebfa1207275d71cb009f201c118b00cf8e77641c7f1e63e330ba909842c009af375c0f5c1c7368e8d7e2066168c40ce3cb629cf212f";
	strTestPubKey = "04d244288a8c6ebbf491443ebfa1207275d71cb009f201c118b00cf8e77641c7f1e63e330ba909842c009af375c0f5c1c7368e8d7e2066168c40ce3cb629cf212f";

	// v2.0.0.7 operative spork pubkey -- newly generated for this
	// release cycle.  Corresponds to XDN mainnet address
	// dFf3hK2WyJ3bkPM7zn52PPyp7sAyvjhAN4.  Private key is held by
	// the current project owner.  This is the key used to sign
	// sporks broadcast from v2.0.0.7 onward.  Testnet shares this
	// pubkey because XDN has historically had no operational testnet;
	// if/when an operational testnet is brought up, split this into
	// a separate testnet key.
	strMainPubKeyNew = "0442731a54d74177a4b1220e06743fd8d1ac9c72206cf6a8653e78de33f2c654cbcba4531b58b5670cfb3810486d63d6fdab05eba5c92e35f72918efb82efb8846";
	strTestPubKeyNew = "0442731a54d74177a4b1220e06743fd8d1ac9c72206cf6a8653e78de33f2c654cbcba4531b58b5670cfb3810486d63d6fdab05eba5c92e35f72918efb82efb8846";
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
	if(id == SPORK_14_TEST_SIGNATURES)					return "SPORK_14_TEST_SIGNATURES";

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
	if(strName == "SPORK_14_TEST_SIGNATURES")					return SPORK_14_TEST_SIGNATURES;

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

	// Try v2.0.0.7 operative key first.  This is the key used by
	// project members to sign sporks going forward.
	std::string errorMessage = "";
	CPubKey pubkeyNew(ParseHex(strMainPubKeyNew));

	if(mnEngineSigner.VerifyMessage(pubkeyNew, spork.vchSig, strMessage, errorMessage))
	{
		return true;
	}

	// Fall back to legacy key for backward compatibility.  Any spork
	// signed with the original (pre-v2.0.0.7) key still verifies here.
	std::string errorMessageLegacy = "";
	CPubKey pubkeyLegacy(ParseHex(strMainPubKey));

	return mnEngineSigner.VerifyMessage(pubkeyLegacy, spork.vchSig, strMessage, errorMessageLegacy);
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

