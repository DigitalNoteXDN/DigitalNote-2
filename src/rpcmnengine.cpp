#include "compat.h"

#include <fstream>
#include <iomanip>
#include <boost/lexical_cast.hpp>

#include "enums/rpcerrorcode.h"
#include "init.h"
#include "cactivemasternode.h"
#include "rpcserver.h"
#include "util.h"
#include "cwallet.h"
#include "cwallettx.h"
#include "creservekey.h"
#include "coutput.h"
#include "script.h"
#include "net/cnode.h"
#include "net.h"
#include "ckey.h"
#include "main_extern.h"
#include "cblockindex.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "cmasternodepayments.h"
#include "cmasternodevotetracker.h"
#include "cmasternodevote.h"
#include "cmasternodeconfig.h"
#include "cmasternodeconfigentry.h"
#include "masternode.h"
#include "masternodeman.h"
#include "masternodeconfig.h"
#include "masternode_extern.h"
#include "cmnenginesigner.h"
#include "mnengine_extern.h"
#include "cdigitalnotesecret.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "thread.h"
#include "rpcprotocol.h"
#include "netbase.h"

void SendMoney(const CTxDestination &address, CAmount nValue, CWalletTx& wtxNew, AvailableCoinsType coin_type=ALL_COINS)
{
	// Check amount
	if (nValue <= 0)
	{
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");
	}

	if (nValue > pwalletMain->GetBalance())
	{
		throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");
	}

	std::string strError;
	if (pwalletMain->IsLocked())
	{
		strError = "Error: Wallet locked, unable to create transaction!";
		
		LogPrintf("SendMoney() : %s", strError);
		
		throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}

	// Parse DigitalNote address
	CScript scriptPubKey = GetScriptForDestination(address);

	// Create and send the transaction
	CReserveKey reservekey(pwalletMain);
	int64_t nFeeRequired;
	std::string sNarr;

	if (!pwalletMain->CreateTransaction(scriptPubKey, nValue, sNarr, wtxNew, reservekey, nFeeRequired, NULL))
	{
		if (nValue + nFeeRequired > pwalletMain->GetBalance())
		{
			strError = strprintf(
				"Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!",
				FormatMoney(nFeeRequired)
			);
		}
		
		LogPrintf("SendMoney() : %s\n", strError);
		
		throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}

	if (!pwalletMain->CommitTransaction(wtxNew, reservekey))
	{
		throw JSONRPCError(
			RPC_WALLET_ERROR,
			"Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here."
		);
	}
}

json_spirit::Value masternode(const json_spirit::Array& params, bool fHelp)
{
	std::string strCommand;

	if (params.size() >= 1)
	{
		strCommand = params[0].get_str();
	}

	if( fHelp ||
		(
			strCommand != "count" &&
			strCommand != "current" &&
			strCommand != "debug" &&
			strCommand != "genkey" &&
			strCommand != "enforce" &&
			strCommand != "list" &&
			strCommand != "list-conf" &&
			strCommand != "start" &&
			strCommand != "start-alias" &&
			strCommand != "start-many" &&
			strCommand != "status" &&
			strCommand != "stop" &&
			strCommand != "stop-alias" &&
			strCommand != "stop-many" &&
			strCommand != "winners" &&
			strCommand != "connect" &&
			strCommand != "outputs" &&
			strCommand != "vote-many" &&
			strCommand != "vote" &&
			strCommand != "gen-config"
		)
	)
	{
		throw std::runtime_error(
			"masternode \"command\"... ( \"passphrase\" )\n"
			"Set of commands to execute masternode related actions\n"
			"\nArguments:\n"
			"1. \"command\"        (string or set of strings, required) The command to execute\n"
			"2. \"passphrase\"     (string, optional) The wallet passphrase\n"
			"\nAvailable commands:\n"
			"  count        - Print number of all known masternodes (optional: 'enabled', 'both')\n"
			"  current      - Print info on current masternode winner\n"
			"  debug        - Print masternode status\n"
			"  genkey       - Generate new masternodeprivkey\n"
			"  enforce      - Enforce masternode payments\n"
			"  list         - Print list of all known masternodes (see masternodelist for more info)\n"
			"  list-conf    - Print masternode.conf in JSON format\n"
			"  outputs      - Print masternode compatible outputs\n"
			"  start        - Start masternode configured in DigitalNote.conf\n"
			"  start-alias  - Start single masternode by assigned alias configured in masternode.conf\n"
			"  start-many   - Start all masternodes configured in masternode.conf\n"
			"  status       - Print masternode status information\n"
			"  stop         - Stop masternode configured in DigitalNote.conf\n"
			"  stop-alias   - Stop single masternode by assigned alias configured in masternode.conf\n"
			"  stop-many    - Stop all masternodes configured in masternode.conf\n"
			"  winners      - Print list of masternode winners\n"
			"  vote-many    - Vote on a DigitalNote initiative\n"
			"  vote         - Vote on a DigitalNote initiative\n"
			"  gen-config   - Generate masternode.conf of current running session.\n"
		);
	}

	if (strCommand == "stop")
	{
		if(!fMasterNode)
		{
			return "you must set masternode=1 in the configuration";
		}
		
		if(pwalletMain->IsLocked())
		{
			SecureString strWalletPass;
			strWalletPass.reserve(100);

			if (params.size() == 2)
			{
				strWalletPass = params[1].get_str().c_str();
			}
			else
			{
				throw std::runtime_error("Your wallet is locked, passphrase is required\n");
			}

			if(!pwalletMain->Unlock(strWalletPass))
			{
				return "incorrect passphrase";
			}
		}

		std::string errorMessage;
		if(!activeMasternode.StopMasterNode(errorMessage))
		{
			return "stop failed: " + errorMessage;
		}
		
		pwalletMain->Lock();
		
		switch(activeMasternode.status)
		{
			case MASTERNODE_STOPPED:
				return "successfully stopped masternode";
			
			case MASTERNODE_NOT_CAPABLE:
				return "not capable masternode";
		}
		
		return "unknown";
	}

	if (strCommand == "stop-alias")
	{
		if (params.size() < 2)
		{
			throw std::runtime_error("command needs at least 2 parameters\n");
		}

		std::string alias = params[1].get_str().c_str();

		if(pwalletMain->IsLocked())
		{
			SecureString strWalletPass;
			strWalletPass.reserve(100);

			if (params.size() == 3)
			{
				strWalletPass = params[2].get_str().c_str();
			}
			else
			{
				throw std::runtime_error("Your wallet is locked, passphrase is required\n");
			}

			if(!pwalletMain->Unlock(strWalletPass))
			{
				return "incorrect passphrase";
			}
		}

		bool found = false;

		json_spirit::Object statusObj;
		statusObj.push_back(json_spirit::Pair("alias", alias));

		for(CMasternodeConfigEntry mne : masternodeConfig.getEntries())
		{
			if(mne.getAlias() == alias)
			{
				found = true;
				std::string errorMessage;
				bool result = activeMasternode.StopMasterNode(mne.getIp(), mne.getPrivKey(), errorMessage);

				statusObj.push_back(json_spirit::Pair("result", result ? "successful" : "failed"));
				
				if(!result)
				{
					statusObj.push_back(json_spirit::Pair("errorMessage", errorMessage));
				}
				
				break;
			}
		}

		if(!found)
		{
			statusObj.push_back(json_spirit::Pair("result", "failed"));
			statusObj.push_back(json_spirit::Pair("errorMessage", "could not find alias in config. Verify with list-conf."));
		}

		pwalletMain->Lock();
		
		return statusObj;
	}

	if (strCommand == "stop-many")
	{
		if(pwalletMain->IsLocked())
		{
			SecureString strWalletPass;
			strWalletPass.reserve(100);

			if (params.size() == 2)
			{
				strWalletPass = params[1].get_str().c_str();
			}
			else
			{
				throw std::runtime_error("Your wallet is locked, passphrase is required\n");
			}

			if(!pwalletMain->Unlock(strWalletPass))
			{
				return "incorrect passphrase";
			}
		}

		int total = 0;
		int successful = 0;
		int fail = 0;
		
		json_spirit::Object resultsObj;

		for(CMasternodeConfigEntry mne : masternodeConfig.getEntries())
		{
			total++;

			std::string errorMessage;
			bool result = activeMasternode.StopMasterNode(mne.getIp(), mne.getPrivKey(), errorMessage);

			json_spirit::Object statusObj;
			statusObj.push_back(json_spirit::Pair("alias", mne.getAlias()));
			statusObj.push_back(json_spirit::Pair("result", result ? "successful" : "failed"));

			if(result)
			{
				successful++;
			}
			else
			{
				fail++;
				
				statusObj.push_back(json_spirit::Pair("errorMessage", errorMessage));
			}

			resultsObj.push_back(json_spirit::Pair("status", statusObj));
		}
		
		pwalletMain->Lock();

		json_spirit::Object returnObj;
		
		returnObj.push_back(
			json_spirit::Pair(
				"overall", "Successfully stopped " + boost::lexical_cast<std::string>(successful) + 
				" masternodes, failed to stop " + boost::lexical_cast<std::string>(fail) +
				", total " + boost::lexical_cast<std::string>(total)
			)
		);
		returnObj.push_back(json_spirit::Pair("detail", resultsObj));

		return returnObj;
	}

	if (strCommand == "list")
	{
		json_spirit::Array newParams(params.size() - 1);
		
		std::copy(params.begin() + 1, params.end(), newParams.begin());
		
		return masternodelist(newParams, fHelp);
	}

	if (strCommand == "count")
	{
		if (params.size() > 2)
		{
			throw std::runtime_error("too many parameters\n");
		}

		if (params.size() == 2)
		{
			if(params[1] == "enabled")
			{
				return mnodeman.CountEnabled();
			}
			
			if(params[1] == "both")
			{
				return boost::lexical_cast<std::string>(mnodeman.CountEnabled()) + " / " +
						boost::lexical_cast<std::string>(mnodeman.size());
			}
		}
		
		return mnodeman.size();
	}

	if (strCommand == "start")
	{
		if(!fMasterNode)
		{
			return "you must set masternode=1 in the configuration";
		}
		
		if(pwalletMain->IsLocked())
		{
			SecureString strWalletPass;
			strWalletPass.reserve(100);

			if (params.size() == 2)
			{
				strWalletPass = params[1].get_str().c_str();
			}
			else
			{
				throw std::runtime_error("Your wallet is locked, passphrase is required\n");
			}

			if(!pwalletMain->Unlock(strWalletPass))
			{
				return "incorrect passphrase";
			}
		}

		if(activeMasternode.status != MASTERNODE_REMOTELY_ENABLED && activeMasternode.status != MASTERNODE_IS_CAPABLE)
		{
			activeMasternode.status = MASTERNODE_NOT_PROCESSED; // TODO: consider better way
			std::string errorMessage;
			
			activeMasternode.ManageStatus();
			pwalletMain->Lock();
		}

		switch(activeMasternode.status)
		{
			case MASTERNODE_REMOTELY_ENABLED:
				return "masternode started remotely";
			
			case MASTERNODE_INPUT_TOO_NEW:
				return "masternode input must have at least 15 confirmations";
			
			case MASTERNODE_STOPPED:
				return "masternode is stopped";
			
			case MASTERNODE_IS_CAPABLE:
				return "successfully started masternode";
			
			case MASTERNODE_NOT_CAPABLE:
				return "not capable masternode: " + activeMasternode.notCapableReason;
			
			case MASTERNODE_SYNC_IN_PROCESS:
				return "sync in process. Must wait until client is synced to start.";
		}
		
		return "unknown";
	}

	if (strCommand == "start-alias")
	{
		if (params.size() < 2)
		{
			throw std::runtime_error("command needs at least 2 parameters\n");
		}

		std::string alias = params[1].get_str().c_str();

		if(pwalletMain->IsLocked())
		{
			SecureString strWalletPass;
			strWalletPass.reserve(100);

			if (params.size() == 3)
			{
				strWalletPass = params[2].get_str().c_str();
			}
			else
			{
				throw std::runtime_error("Your wallet is locked, passphrase is required\n");
			}

			if(!pwalletMain->Unlock(strWalletPass))
			{
				return "incorrect passphrase";
			}
		}

		bool found = false;

		json_spirit::Object statusObj;
		statusObj.push_back(json_spirit::Pair("alias", alias));

		for(CMasternodeConfigEntry mne : masternodeConfig.getEntries())
		{
			if(mne.getAlias() == alias)
			{
				found = true;
				std::string errorMessage;
				std::string strDonateAddress = "";
				std::string strDonationPercentage = "";

				bool result = activeMasternode.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strDonateAddress, strDonationPercentage, errorMessage);

				statusObj.push_back(json_spirit::Pair("result", result ? "successful" : "failed"));
				
				if(!result)
				{
					statusObj.push_back(json_spirit::Pair("errorMessage", errorMessage));
				}
				
				break;
			}
		}

		if(!found)
		{
			statusObj.push_back(json_spirit::Pair("result", "failed"));
			statusObj.push_back(json_spirit::Pair("errorMessage", "could not find alias in config. Verify with list-conf."));
		}

		pwalletMain->Lock();
		
		return statusObj;

	}

	if (strCommand == "start-many")
	{
		if(pwalletMain->IsLocked())
		{
			SecureString strWalletPass;
			strWalletPass.reserve(100);

			if (params.size() == 2)
			{
				strWalletPass = params[1].get_str().c_str();
			}
			else
			{
				throw std::runtime_error(
					"Your wallet is locked, passphrase is required\n"
				);
			}

			if(!pwalletMain->Unlock(strWalletPass))
			{
				return "incorrect passphrase";
			}
		}

		std::vector<CMasternodeConfigEntry> mnEntries;
		mnEntries = masternodeConfig.getEntries();

		int total = 0;
		int successful = 0;
		int fail = 0;

		json_spirit::Object resultsObj;

		for(CMasternodeConfigEntry mne : masternodeConfig.getEntries())
		{
			total++;

			std::string errorMessage;
			std::string strDonateAddress = "";
			std::string strDonationPercentage = "";

			bool result = activeMasternode.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strDonateAddress, strDonationPercentage, errorMessage);

			json_spirit::Object statusObj;
			statusObj.push_back(json_spirit::Pair("alias", mne.getAlias()));
			statusObj.push_back(json_spirit::Pair("result", result ? "succesful" : "failed"));

			if(result)
			{
				successful++;
			}
			else
			{
				fail++;
				
				statusObj.push_back(json_spirit::Pair("errorMessage", errorMessage));
			}

			resultsObj.push_back(json_spirit::Pair("status", statusObj));
		}
		
		pwalletMain->Lock();

		json_spirit::Object returnObj;
		returnObj.push_back(
			json_spirit::Pair(
				"overall", "Successfully started " + boost::lexical_cast<std::string>(successful) +
				" masternodes, failed to start " + boost::lexical_cast<std::string>(fail) +
				", total " + boost::lexical_cast<std::string>(total)
			)
		);
		returnObj.push_back(json_spirit::Pair("detail", resultsObj));

		return returnObj;
	}

	if (strCommand == "debug")
	{
		switch(activeMasternode.status)
		{
			case MASTERNODE_REMOTELY_ENABLED:
				return "masternode started remotely";
			
			case MASTERNODE_INPUT_TOO_NEW:
				return "masternode input must have at least 15 confirmations";
				
			case MASTERNODE_IS_CAPABLE:
				return "successfully started masternode";
			
			case MASTERNODE_STOPPED:
				return "masternode is stopped";
			
			case MASTERNODE_NOT_CAPABLE:
				return "not capable masternode: " + activeMasternode.notCapableReason;
			
			case MASTERNODE_SYNC_IN_PROCESS:
				return "sync in process. Must wait until client is synced to start.";
		}
		
		CTxIn vin = CTxIn();
		CPubKey pubkey = CScript();
		CKey key;
		bool found = activeMasternode.GetMasterNodeVin(vin, pubkey, key);
		
		if(!found)
		{
			return "Missing masternode input, please look at the documentation for instructions on masternode creation";
		}
		else
		{
			return "No problems were found";
		}
	}

	if (strCommand == "create")
	{
		return "Not implemented yet, please look at the documentation for instructions on masternode creation";
	}

	if (strCommand == "current")
	{
		CMasternode* winner = mnodeman.GetCurrentMasterNode(1);
		
		if(winner)
		{
			json_spirit::Object obj;
			CScript pubkey;
			pubkey.SetDestination(winner->pubkey.GetID());
			CTxDestination address1;
			ExtractDestination(pubkey, address1);
			CDigitalNoteAddress address2(address1);

			obj.push_back(json_spirit::Pair("IP:port", winner->addr.ToString().c_str()));
			obj.push_back(json_spirit::Pair("protocol", (int64_t)winner->protocolVersion));
			obj.push_back(json_spirit::Pair("vin", winner->vin.prevout.hash.ToString().c_str()));
			obj.push_back(json_spirit::Pair("pubkey", address2.ToString().c_str()));
			obj.push_back(json_spirit::Pair("lastseen", (int64_t)winner->lastTimeSeen));
			obj.push_back(json_spirit::Pair("activeseconds", (int64_t)(winner->lastTimeSeen - winner->sigTime)));
			
			return obj;
		}

		return "unknown";
	}

	if (strCommand == "genkey")
	{
		CKey secret;
		
		secret.MakeNewKey(false);

		return CDigitalNoteSecret(secret).ToString();
	}

	if (strCommand == "winners")
	{
		json_spirit::Object obj;
		std::string strMode = "addr";

		if (params.size() >= 1)
		{
			strMode = params[0].get_str();
		}
		
		for(int nHeight = pindexBest->nHeight-10; nHeight < pindexBest->nHeight+20; nHeight++)
		{
			CScript payee;
			CTxIn vin;
			
			if(masternodePayments.GetBlockPayee(nHeight, payee, vin))
			{
				CTxDestination address1;
				ExtractDestination(payee, address1);
				CDigitalNoteAddress address2(address1);

				if(strMode == "addr")
				{
					obj.push_back(json_spirit::Pair(boost::lexical_cast<std::string>(nHeight), address2.ToString().c_str()));
				}
				
				if(strMode == "vin")
				{
					obj.push_back(json_spirit::Pair(boost::lexical_cast<std::string>(nHeight), vin.ToString().c_str()));
				}
			}
			else
			{
				obj.push_back(json_spirit::Pair(boost::lexical_cast<std::string>(nHeight), ""));
			}
		}

		return obj;
	}

	if(strCommand == "enforce")
	{
		return (uint64_t)enforceMasternodePaymentsTime;
	}

	if(strCommand == "connect")
	{
		std::string strAddress = "";
		
		if (params.size() == 2)
		{
			strAddress = params[1].get_str().c_str();
		}
		else
		{
			throw std::runtime_error("Masternode address required\n");
		}

		CService addr = CService(strAddress);

		if(ConnectNode((CAddress)addr, NULL, true))
		{
			return "successfully connected";
		}
		else
		{
			return "error connecting";
		}
	}

	if(strCommand == "list-conf")
	{
		std::vector<CMasternodeConfigEntry> mnEntries;
		mnEntries = masternodeConfig.getEntries();

		json_spirit::Object resultObj;

		for(CMasternodeConfigEntry mne : masternodeConfig.getEntries())
		{
			json_spirit::Object mnObj;
			
			mnObj.push_back(json_spirit::Pair("alias", mne.getAlias()));
			mnObj.push_back(json_spirit::Pair("address", mne.getIp()));
			mnObj.push_back(json_spirit::Pair("privateKey", mne.getPrivKey()));
			mnObj.push_back(json_spirit::Pair("txHash", mne.getTxHash()));
			mnObj.push_back(json_spirit::Pair("outputIndex", mne.getOutputIndex()));
			
			resultObj.push_back(json_spirit::Pair("masternode", mnObj));
		}

		return resultObj;
	}

	if (strCommand == "outputs")
	{
		// Find possible candidates
		std::vector<COutput> possibleCoins = activeMasternode.SelectCoinsMasternode();
		
		json_spirit::Array results;
		
		for(COutput& out : possibleCoins)
		{
			json_spirit::Object obj;
			
			obj.push_back(json_spirit::Pair("txhash", out.tx->GetHash().ToString().c_str()));
			obj.push_back(json_spirit::Pair("outputidx", boost::lexical_cast<std::string>(out.i)));
			
			results.push_back(obj);
		}

		return results;
	}

	if(strCommand == "vote-many")
	{
		std::vector<CMasternodeConfigEntry> mnEntries;
		mnEntries = masternodeConfig.getEntries();

		if (params.size() != 2)
		{
			throw std::runtime_error("You can only vote 'yay' or 'nay'");
		}
		
		std::string vote = params[1].get_str().c_str();
		if(vote != "yay" && vote != "nay")
		{
			return "You can only vote 'yay' or 'nay'";
		}
		
		int nVote = 0;
		
		if(vote == "yay")
		{
			nVote = 1;
		}
		
		if(vote == "nay")
		{
			nVote = -1;
		}

		int success = 0;
		int failed = 0;

		json_spirit::Object resultObj;

		for(CMasternodeConfigEntry mne : masternodeConfig.getEntries())
		{
			std::string errorMessage;
			std::vector<unsigned char> vchMasterNodeSignature;
			std::string strMasterNodeSignMessage;

			CPubKey pubKeyCollateralAddress;
			CKey keyCollateralAddress;
			CPubKey pubKeyMasternode;
			CKey keyMasternode;

			if(!mnEngineSigner.SetKey(mne.getPrivKey(), errorMessage, keyMasternode, pubKeyMasternode))
			{
				printf(" Error upon calling SetKey for %s\n", mne.getAlias().c_str());
				
				failed++;
				
				continue;
			}

			CMasternode* pmn = mnodeman.Find(pubKeyMasternode);
			if(pmn == NULL)
			{
				printf("Can't find masternode by pubkey for %s\n", mne.getAlias().c_str());
				
				failed++;
				
				continue;
			}

			std::string strMessage = pmn->vin.ToString() + boost::lexical_cast<std::string>(nVote);

			if(!mnEngineSigner.SignMessage(strMessage, errorMessage, vchMasterNodeSignature, keyMasternode))
			{
				printf(" Error upon calling SignMessage for %s\n", mne.getAlias().c_str());
				
				failed++;
				
				continue;
			}

			if(!mnEngineSigner.VerifyMessage(pubKeyMasternode, vchMasterNodeSignature, strMessage, errorMessage))
			{
				printf(" Error upon calling VerifyMessage for %s\n", mne.getAlias().c_str());
				
				failed++;
				
				continue;
			}

			success++;

			//send to all peers
			LOCK(cs_vNodes);
			
			for(CNode* pnode : vNodes)
			{
				pnode->PushMessage("mvote", pmn->vin, vchMasterNodeSignature, nVote);
			}
		}

		return("Voted successfully " + boost::lexical_cast<std::string>(success) + " time(s) and failed " + boost::lexical_cast<std::string>(failed) + " time(s).");
	}

	if(strCommand == "vote")
	{
		std::vector<CMasternodeConfigEntry> mnEntries;
		mnEntries = masternodeConfig.getEntries();

		if (params.size() != 2)
		{
			throw std::runtime_error("You can only vote 'yay' or 'nay'");
		}
		
		std::string vote = params[1].get_str().c_str();
		if(vote != "yay" && vote != "nay")
		{
			return "You can only vote 'yay' or 'nay'";
		}
		
		int nVote = 0;
		
		if(vote == "yay")
		{
			nVote = 1;
		}
		
		if(vote == "nay")
		{
			nVote = -1;
		}
		
		// Choose coins to use
		CPubKey pubKeyCollateralAddress;
		CKey keyCollateralAddress;
		CPubKey pubKeyMasternode;
		CKey keyMasternode;

		std::string errorMessage;
		std::vector<unsigned char> vchMasterNodeSignature;
		std::string strMessage = activeMasternode.vin.ToString() + boost::lexical_cast<std::string>(nVote);

		if(!mnEngineSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode))
		{
			return(" Error upon calling SetKey");
		}
		
		if(!mnEngineSigner.SignMessage(strMessage, errorMessage, vchMasterNodeSignature, keyMasternode))
		{
			return(" Error upon calling SignMessage");
		}

		if(!mnEngineSigner.VerifyMessage(pubKeyMasternode, vchMasterNodeSignature, strMessage, errorMessage))
		{
			return(" Error upon calling VerifyMessage");
		}
		
		//send to all peers
		LOCK(cs_vNodes);
		
		for(CNode* pnode : vNodes)
		{
			pnode->PushMessage("mvote", activeMasternode.vin, vchMasterNodeSignature, nVote);
		}
	}

	if(strCommand == "status")
	{
		std::vector<CMasternodeConfigEntry> mnEntries;
		mnEntries = masternodeConfig.getEntries();

		CScript pubkey;
		pubkey = GetScriptForDestination(activeMasternode.pubKeyMasternode.GetID());
		
		CTxDestination address1;
		ExtractDestination(pubkey, address1);
		CDigitalNoteAddress address2(address1);

		json_spirit::Object mnObj;
		CMasternode *pmn = mnodeman.Find(activeMasternode.vin);

		mnObj.push_back(json_spirit::Pair("vin", activeMasternode.vin.ToString().c_str()));
		mnObj.push_back(json_spirit::Pair("service", activeMasternode.service.ToString().c_str()));
		mnObj.push_back(json_spirit::Pair("status", activeMasternode.status));
		//mnObj.push_back(json_spirit::Pair("pubKeyMasternode", address2.ToString().c_str()));
		
		if (pmn)
		{
			mnObj.push_back(json_spirit::Pair("pubkey", CDigitalNoteAddress(pmn->pubkey.GetID()).ToString()));
		}
		
		mnObj.push_back(json_spirit::Pair("notCapableReason", activeMasternode.notCapableReason.c_str()));

		return mnObj;
	}

	if(strCommand == "gen-config")
	{
		json_spirit::Object mnObj;
		std::string config = "<server_name> ";
		std::string strMasterNodePrivKey = GetArg("-masternodeprivkey", "");
		
		// Masternode server address
		config += activeMasternode.service.ToString();
		config += " ";
		
		// Private master node key
		if (strMasterNodePrivKey != "")
		{
			config += strMasterNodePrivKey;
			config += " ";
		}
		else
		{
			config += "<masternodeprivkey> ";
		}
		
		// 2 million coints input hash and position
		config += activeMasternode.vin.prevout.hash.ToString();
		config += " ";
		config += boost::lexical_cast<std::string>(activeMasternode.vin.prevout.n);
		
		mnObj.push_back(json_spirit::Pair("config", config.c_str()));
		
		return mnObj;
	}

	return json_spirit::Value::null;
}

json_spirit::Value masternodelist(const json_spirit::Array& params, bool fHelp)
{
	std::string strMode = "status";
	std::string strFilter = "";

	if (params.size() >= 1)
	{
		strMode = params[0].get_str();
	}

	if (params.size() == 2)
	{
		strFilter = params[1].get_str();
	}

	if (fHelp ||
		(
			strMode != "activeseconds" &&
			strMode != "donation" &&
			strMode != "full" &&
			strMode != "lastseen" &&
			strMode != "protocol" &&
			strMode != "pubkey" &&
			strMode != "rank" &&
			strMode != "status" &&
			strMode != "addr" &&
			strMode != "votes" &&
			strMode != "lastpaid"
		)
	)
	{
		throw std::runtime_error(
			"masternodelist ( \"mode\" \"filter\" )\n"
			"Get a list of masternodes in different modes\n"
			"\nArguments:\n"
			"1. \"mode\"      (string, optional/required to use filter, defaults = status) The mode to run list in\n"
			"2. \"filter\"    (string, optional) Filter results. Partial match by IP by default in all modes, additional matches in some modes\n"
			"\nAvailable modes:\n"
			"  activeseconds  - Print number of seconds masternode recognized by the network as enabled\n"
			"  donation       - Show donation settings\n"
			"  full           - Print info in format 'status protocol pubkey vin lastseen activeseconds' (can be additionally filtered, partial match)\n"
			"  lastseen       - Print timestamp of when a masternode was last seen on the network\n"
			"  protocol       - Print protocol of a masternode (can be additionally filtered, exact match)\n"
			"  pubkey         - Print public key associated with a masternode (can be additionally filtered, partial match)\n"
			"  rank           - Print rank of a masternode based on current block\n"
			"  status         - Print masternode status: ENABLED / EXPIRED / VIN_SPENT / REMOVE / POS_ERROR (can be additionally filtered, partial match)\n"
			"  addr            - Print ip address associated with a masternode (can be additionally filtered, partial match)\n"
			"  votes          - Print all masternode votes for a DigitalNote initiative (can be additionally filtered, partial match)\n"
			"  lastpaid       - The last time a node was paid on the network\n"
		);
	}

	json_spirit::Object obj;
	json_spirit::Array full_results;
	
	if (strMode == "rank" || strMode == "full")
	{
		std::vector<std::pair<int, CMasternode> > vMasternodeRanks = mnodeman.GetMasternodeRanks(pindexBest->nHeight);
		
		
		for(std::pair<int, CMasternode>& s : vMasternodeRanks)
		{
			std::string strVin = s.second.vin.prevout.ToStringShort();
			
			if(strFilter != "" && strVin.find(strFilter) == std::string::npos)
			{
				continue;
			}
			
			if(strMode == "rank")
			{
				obj.push_back(json_spirit::Pair(strVin, s.first));
			}
			else if (strMode == "full")
			{
				CMasternode& mn = s.second;
				
				CScript pubkey;
				pubkey.SetDestination(mn.pubkey.GetID());
				CTxDestination address1;
				ExtractDestination(pubkey, address1);
				CDigitalNoteAddress address2(address1);

				std::ostringstream addrStream;
				addrStream << strVin;
				
				std::string strStatus = mn.Status();
				std::string strNetwork = GetNetworkName(mn.addr.GetNetwork());
				
				json_spirit::Object output;
				output.push_back(json_spirit::Pair("rank", (strStatus == "ENABLED" ? s.first : 0)));
				output.push_back(json_spirit::Pair("network", strNetwork));
				output.push_back(json_spirit::Pair("txhash", mn.vin.prevout.hash.ToString().c_str()));
				output.push_back(json_spirit::Pair("outidx", (uint64_t)mn.vin.prevout.n));
				output.push_back(json_spirit::Pair("status", strStatus));
				output.push_back(json_spirit::Pair("addr", address2.ToString().c_str()));
				output.push_back(json_spirit::Pair("version", mn.protocolVersion));
				output.push_back(json_spirit::Pair("lastseen", (int64_t)mn.lastTimeSeen));
				output.push_back(json_spirit::Pair("activetime", (int64_t)(mn.lastTimeSeen - mn.sigTime)));
				output.push_back(json_spirit::Pair("lastpaid", (int64_t)mn.nLastPaid));
				
				full_results.push_back(output);
			}
		}
		
		if(strMode == "full")
		{
			return full_results;
		}
	}
	else
	{
		std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeVector();
		
		for(CMasternode& mn : vMasternodes)
		{
			std::string strVin = mn.vin.prevout.ToStringShort();
			
			if (strMode == "activeseconds")
			{
				if(strFilter !="" && strVin.find(strFilter) == std::string::npos)
				{
					continue;
				}
				
				obj.push_back(json_spirit::Pair(strVin,(int64_t)(mn.lastTimeSeen - mn.sigTime)));
			}
			else if (strMode == "donation")
			{
				CTxDestination address1;
				ExtractDestination(mn.donationAddress, address1);
				CDigitalNoteAddress address2(address1);

				if(strFilter !="" &&
					address2.ToString().find(strFilter) == std::string::npos &&
					strVin.find(strFilter) == std::string::npos
				)
				{
					continue;
				}

				std::string strOut = "";

				if(mn.donationPercentage != 0)
				{
					strOut = address2.ToString().c_str();
					strOut += ":";
					strOut += boost::lexical_cast<std::string>(mn.donationPercentage);
				}
				
				obj.push_back(json_spirit::Pair(strVin, strOut.c_str()));
			}
			else if (strMode == "lastseen")
			{
				if(strFilter !="" &&
					strVin.find(strFilter) == std::string::npos)
				{
					continue;
				}
				
				obj.push_back(json_spirit::Pair(strVin, (int64_t)mn.lastTimeSeen));
			}
			else if (strMode == "protocol")
			{
				if(strFilter !="" &&
					strFilter != boost::lexical_cast<std::string>(mn.protocolVersion) &&
					strVin.find(strFilter) == std::string::npos)
				{
					continue;
				}
				
				obj.push_back(json_spirit::Pair(strVin, (int64_t)mn.protocolVersion));
			}
			else if (strMode == "pubkey")
			{
				CScript pubkey;
				pubkey.SetDestination(mn.pubkey.GetID());
				CTxDestination address1;
				ExtractDestination(pubkey, address1);
				CDigitalNoteAddress address2(address1);

				if(strFilter !="" &&
					address2.ToString().find(strFilter) == std::string::npos &&
					strVin.find(strFilter) == std::string::npos
				)
				{
					continue;
				}
				
				obj.push_back(json_spirit::Pair(strVin, address2.ToString().c_str()));
			}
			else if(strMode == "status")
			{
				std::string strStatus = mn.Status();
				
				if(strFilter != "" &&
					strVin.find(strFilter) == std::string::npos &&
					strStatus.find(strFilter) == std::string::npos
				)
				{
					continue;
				}
				
				obj.push_back(json_spirit::Pair(strVin, strStatus.c_str()));
			}
			else if (strMode == "addr")
			{
				if(strFilter !="" &&
					mn.vin.prevout.hash.ToString().find(strFilter) == std::string::npos &&
					strVin.find(strFilter) == std::string::npos
				)
				{
					continue;
				}
				
				obj.push_back(json_spirit::Pair(strVin, mn.addr.ToString().c_str()));
			}
			else if(strMode == "votes")
			{
				std::string strStatus = "ABSTAIN";

				//voting lasts 30 days, ignore the last vote if it was older than that
				if((GetAdjustedTime() - mn.lastVote) < (60*60*30*24))
				{
					if(mn.nVote == -1)
					{
						strStatus = "NAY";
					}
					
					if(mn.nVote == 1)
					{
						strStatus = "YAY";
					}
				}

				if(
					strFilter != "" &&
					(
						strVin.find(strFilter) == std::string::npos &&
						strStatus.find(strFilter) == std::string::npos
					)
				)
				{
					continue;
				}
				
				obj.push_back(json_spirit::Pair(strVin, strStatus.c_str()));
			}
			else if(strMode == "lastpaid")
			{
				if(strFilter !="" &&
					mn.vin.prevout.hash.ToString().find(strFilter) == std::string::npos &&
					strVin.find(strFilter) == std::string::npos
				)
				{
					continue;
				}
				
				obj.push_back(json_spirit::Pair(strVin, (int64_t)mn.nLastPaid));
			}
		}
	}

	return obj;
}


// ===========================================================================
// v2.0.0.8 M1: getmnlastpaid RPC
//
// Reports chain-derived lastPaidHeight cache contents.  Useful for:
//   - Verifying initial population walk worked correctly
//   - Comparing across nodes (should match given same chain state)
//   - Debugging mapLastPaidHeight cache during testing
//
// This is read-only inspection.  It does NOT trigger any state change.
// ===========================================================================

json_spirit::Value getmnlastpaid(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() > 1)
	{
		throw std::runtime_error(
			"getmnlastpaid [\"vin\"]\n"
			"\nReports the chain-derived last-paid-height for each enabled masternode.\n"
			"\nArguments:\n"
			"1. \"vin\"        (string, optional) If supplied, only report this MN's "
			"entry.  Format: \"txid:vout\".\n"
			"\nResult:\n"
			"{\n"
			"  \"chain_height\": n,             (numeric) current chain tip height\n"
			"  \"scanned_to_height\": n,        (numeric) oldest block scanned by initial walk\n"
			"  \"masternodes\": [               (array) one entry per enabled masternode\n"
			"    {\n"
			"      \"vin\": \"txid-prefix:n\",  (string) collateral outpoint\n"
			"      \"address\": \"...\",        (string) MN payment address\n"
			"      \"last_paid_height\": n,     (numeric) most recent payment block, "
			"or 0 if none in scanned range\n"
			"      \"blocks_since_paid\": n,    (numeric) chain_height - last_paid_height, "
			"or chain_height if never seen\n"
			"      \"voting_recently\": bool,   (boolean) whether this MN has been "
			"seen voting in the active window (~last 10 blocks).  v2.0.0.7 MNs always "
			"show false; broken v2.0.0.8 MNs may also show false.\n"
			"      \"last_vote_height\": n      (numeric) most recent height this MN "
			"voted for in our local tally, or 0 if not seen.\n"
			"    }, ...\n"
			"  ]\n"
			"}\n"
		);
	}

	json_spirit::Object obj;

	int chainHeight = (pindexBest != NULL) ? pindexBest->nHeight : 0;
	obj.push_back(json_spirit::Pair("chain_height", chainHeight));

	// Filter argument: optional "txid:vout" string.
	bool fHasFilter = false;
	COutPoint filterOutpoint;

	if (params.size() == 1)
	{
		std::string strVin = params[0].get_str();
		std::string::size_type colon = strVin.find(':');

		if (colon == std::string::npos)
		{
			throw std::runtime_error(
				"Invalid vin filter format -- expected \"txid:vout\""
			);
		}

		std::string strTxid = strVin.substr(0, colon);
		std::string strVout = strVin.substr(colon + 1);

		uint256 hash;
		hash.SetHex(strTxid);

		unsigned int n;
		try
		{
			n = boost::lexical_cast<unsigned int>(strVout);
		}
		catch (boost::bad_lexical_cast&)
		{
			throw std::runtime_error("Invalid vout in vin filter");
		}

		filterOutpoint = COutPoint(hash, n);
		fHasFilter = true;
	}

	json_spirit::Array masternodes;

	// Snapshot MN list to avoid holding cs across the JSON building.
	std::vector<CMasternode> snapshot = mnodeman.GetFullMasternodeVector();

	// M3 patch 1: snapshot voting activity so we can annotate each MN with
	// "is this MN actually voting within the active window."  Useful for
	// spotting MNs that send dseep but aren't operating correctly (broken
	// chain view -> votes rejected by validity window).
	std::map<COutPoint, int> voterActivity = voteTracker.GetVoterActivity();

	for (CMasternode& mn : snapshot)
	{
		if (!mn.IsEnabled())
		{
			continue;
		}

		if (fHasFilter && !(mn.vin.prevout == filterOutpoint))
		{
			continue;
		}

		int lastPaidHeight = mnodeman.GetLastPaidHeight(mn.vin.prevout);
		int blocksSincePaid = (lastPaidHeight == 0) ? chainHeight : (chainHeight - lastPaidHeight);

		// Look up voting activity for this MN.  If not in voterActivity map,
		// the MN hasn't had a vote land in our mapVotes within the active
		// window (or it's a v2.0.0.7 MN that doesn't vote at all).
		int lastVoteHeight = 0;
		std::map<COutPoint, int>::const_iterator vait = voterActivity.find(mn.vin.prevout);
		if (vait != voterActivity.end())
		{
			lastVoteHeight = vait->second;
		}

		// "Recently" = anywhere in the active vote-tracking window.
		// mapVotes only retains heights within VOTE_PAST_HORIZON of the tip,
		// so presence in voterActivity == voted within the last ~10 blocks.
		bool votingRecently = (lastVoteHeight > 0);

		json_spirit::Object entry;
		entry.push_back(json_spirit::Pair("vin", mn.vin.prevout.ToString()));
		entry.push_back(json_spirit::Pair("address", CDigitalNoteAddress(mn.pubkey.GetID()).ToString()));
		entry.push_back(json_spirit::Pair("last_paid_height", lastPaidHeight));
		entry.push_back(json_spirit::Pair("blocks_since_paid", blocksSincePaid));
		entry.push_back(json_spirit::Pair("voting_recently", votingRecently));
		entry.push_back(json_spirit::Pair("last_vote_height", lastVoteHeight));

		masternodes.push_back(entry);
	}

	obj.push_back(json_spirit::Pair("masternodes", masternodes));

	return obj;
}


// ===========================================================================
// v2.0.0.8 M3: vote-system RPCs
// ===========================================================================

static std::string ScriptToAddress(const CScript &script)
{
	CTxDestination dest;
	if (!ExtractDestination(script, dest))
	{
		return std::string("(unparseable)");
	}
	return CDigitalNoteAddress(dest).ToString();
}

json_spirit::Value getvoteinfo(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() > 1)
	{
		throw std::runtime_error(
			"getvoteinfo [height]\n"
			"\nReports masternode vote tally for a given block height.  If no\n"
			"height is given, uses the current chain tip + VOTE_LOOKAHEAD.\n"
			"\nArguments:\n"
			"1. height       (numeric, optional) The block height to inspect.\n"
			"\nResult:\n"
			"{\n"
			"  \"height\": n,                    (numeric) the queried height\n"
			"  \"eligible_voters\": n,           (numeric) count of MNs at MIN_VOTING_PROTOCOL_VERSION or higher\n"
			"  \"total_votes\": n,               (numeric) total votes received for this height (across all payees)\n"
			"  \"threshold_numerator\": n,       (numeric) consensus threshold (3 for 60%)\n"
			"  \"threshold_denominator\": n,     (numeric) consensus threshold (5 for 60%)\n"
			"  \"has_consensus\": bool,          (boolean) whether any payee reached 60%\n"
			"  \"canonical_payee\": \"...\",     (string) winner address (only if has_consensus is true)\n"
			"  \"canonical_vote_count\": n,      (numeric) vote count for winner (only if has_consensus is true)\n"
			"  \"per_payee\": [\n"
			"    {\n"
			"      \"address\": \"...\",         (string) payee address\n"
			"      \"vote_count\": n,            (numeric) how many MNs voted for this payee\n"
			"      \"voters\": [\"...\"],        (array)  voter vins\n"
			"      \"first_seen\": n             (numeric) unix time of first vote for this payee\n"
			"    }, ...\n"
			"  ]\n"
			"}\n"
		);
	}

	int targetHeight;
	if (params.size() == 1)
	{
		targetHeight = params[0].get_int();
	}
	else
	{
		if (pindexBest == NULL)
		{
			throw std::runtime_error("Chain not loaded");
		}
		targetHeight = pindexBest->nHeight + VOTE_LOOKAHEAD;
	}

	CMasternodeVoteTracker::VoteInfo info = voteTracker.GetVoteInfo(targetHeight);

	json_spirit::Object obj;
	obj.push_back(json_spirit::Pair("height", info.height));
	obj.push_back(json_spirit::Pair("eligible_voters", info.eligibleVoters));
	obj.push_back(json_spirit::Pair("total_votes", info.totalVotes));
	obj.push_back(json_spirit::Pair("threshold_numerator", VOTED_CONSENSUS_THRESHOLD_NUMERATOR));
	obj.push_back(json_spirit::Pair("threshold_denominator", VOTED_CONSENSUS_THRESHOLD_DENOMINATOR));
	obj.push_back(json_spirit::Pair("has_consensus", info.hasConsensus));

	if (info.hasConsensus)
	{
		obj.push_back(json_spirit::Pair("canonical_payee", ScriptToAddress(info.canonicalPayee)));
		obj.push_back(json_spirit::Pair("canonical_vote_count", info.canonicalVoteCount));
	}

	json_spirit::Array perPayee;
	for (size_t i = 0; i < info.perPayee.size(); i++)
	{
		const CMasternodeVoteTracker::VoteInfoEntry &e = info.perPayee[i];

		json_spirit::Object entry;
		entry.push_back(json_spirit::Pair("address", ScriptToAddress(e.payeeScript)));
		entry.push_back(json_spirit::Pair("vote_count", (int)e.voterVins.size()));
		entry.push_back(json_spirit::Pair("first_seen", (int64_t)e.firstSeen));

		json_spirit::Array voters;
		for (std::set<COutPoint>::const_iterator vit = e.voterVins.begin();
			 vit != e.voterVins.end(); ++vit)
		{
			voters.push_back(vit->ToString());
		}
		entry.push_back(json_spirit::Pair("voters", voters));

		perPayee.push_back(entry);
	}
	obj.push_back(json_spirit::Pair("per_payee", perPayee));

	return obj;
}

json_spirit::Value listequivocators(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0)
	{
		throw std::runtime_error(
			"listequivocators\n"
			"\nReports masternodes currently marked as equivocators on THIS node.\n"
			"Equivocator status is per-node; it does not propagate.  Recovery\n"
			"is via fresh dsee (Path A) or the clearequivocator RPC (Path B).\n"
			"\nResult:\n"
			"[\n"
			"  {\n"
			"    \"vin\": \"txid:vout\",         (string) collateral outpoint\n"
			"    \"count\": n,                    (numeric) equivocation events in this session\n"
			"    \"last_equivocation_time\": n,   (numeric) unix time of last event\n"
			"    \"auto_clearing_available\": bool  (boolean) true if count < MAX_EQUIVOCATIONS_PER_SESSION\n"
			"  }, ...\n"
			"]\n"
		);
	}

	std::vector<CMasternodeVoteTracker::EquivocatorInfo> list = voteTracker.GetEquivocatorList();

	json_spirit::Array arr;
	for (size_t i = 0; i < list.size(); i++)
	{
		const CMasternodeVoteTracker::EquivocatorInfo &e = list[i];

		json_spirit::Object entry;
		entry.push_back(json_spirit::Pair("vin", e.voterVin.ToString()));
		entry.push_back(json_spirit::Pair("count", e.count));
		entry.push_back(json_spirit::Pair("last_equivocation_time", e.lastEquivocationTime));
		entry.push_back(json_spirit::Pair("auto_clearing_available", e.autoClearingAvailable));

		arr.push_back(entry);
	}

	return arr;
}

json_spirit::Value clearequivocator(const json_spirit::Array& params, bool fHelp)
{
	if (fHelp || params.size() != 1)
	{
		throw std::runtime_error(
			"clearequivocator \"vin\"\n"
			"\nRemoves an equivocator entry from THIS node's tracker.  Local-only;\n"
			"other nodes still see the MN as an equivocator until they clear it.\n"
			"\nArguments:\n"
			"1. \"vin\"        (string, required) Collateral outpoint as \"txid:vout\"\n"
			"\nResult:\n"
			"{\n"
			"  \"cleared\": bool,                (boolean) true if the MN was in the equivocator map\n"
			"  \"vin\": \"...\"                  (string) the outpoint passed in\n"
			"}\n"
		);
	}

	std::string strVin = params[0].get_str();
	std::string::size_type colon = strVin.find(':');
	if (colon == std::string::npos)
	{
		throw std::runtime_error("Invalid vin format -- expected \"txid:vout\"");
	}

	std::string strTxid = strVin.substr(0, colon);
	std::string strVout = strVin.substr(colon + 1);

	uint256 hash;
	hash.SetHex(strTxid);

	unsigned int n;
	try
	{
		n = boost::lexical_cast<unsigned int>(strVout);
	}
	catch (boost::bad_lexical_cast&)
	{
		throw std::runtime_error("Invalid vout");
	}

	COutPoint outpoint(hash, n);

	bool cleared = voteTracker.ClearEquivocator(outpoint);

	json_spirit::Object obj;
	obj.push_back(json_spirit::Pair("cleared", cleared));
	obj.push_back(json_spirit::Pair("vin", outpoint.ToString()));

	return obj;
}
