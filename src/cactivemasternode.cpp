#include "compat.h"

#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include "clientversion.h"
#include "cwallet.h"
#include "coutput.h"
#include "cwallettx.h"
#include "mining.h"
#include "script.h"
#include "thread.h"
#include "net.h"
#include "ckey.h"
#include "main_extern.h"
#include "cblockindex.h"
#include "util.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "cmasternodevotetracker.h"
#include "cmasternodevotequeue.h"
#include "masternode.h"
#include "masternodeman.h"
#include "masternode_extern.h"
#include "ctxout.h"
#include "cmnenginesigner.h"
#include "cmasternodeconfig.h"
#include "cmasternodeconfigentry.h"
#include "masternodeconfig.h"
#include "mnengine_extern.h"
#include "init.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "net/cnode.h"
#include "version.h"

#include "cactivemasternode.h"

CActiveMasternode::CActiveMasternode()
{
	status = MASTERNODE_NOT_PROCESSED;
}

//
// Bootup the masternode, look for a masternode collateral input and register on the network
//
void CActiveMasternode::ManageStatus()
{
	std::string errorMessage;

	if (fDebug)
	{
		LogPrintf("CActiveMasternode::ManageStatus() - Begin\n");
	}

	if(!fMasterNode)
	{
		return;
	}

	//need correct adjusted time to send ping
	bool fIsInitialDownload = IsInitialBlockDownload();
	if(fIsInitialDownload)
	{
		status = MASTERNODE_SYNC_IN_PROCESS;
		
		LogPrintf("CActiveMasternode::ManageStatus() - Sync in progress. Must wait until sync is complete to start masternode.\n");
		
		return;
	}

	if(status == MASTERNODE_INPUT_TOO_NEW || status == MASTERNODE_NOT_CAPABLE || status == MASTERNODE_SYNC_IN_PROCESS)
	{
		status = MASTERNODE_NOT_PROCESSED;
	}

	if(status == MASTERNODE_NOT_PROCESSED)
	{
		if(strMasterNodeAddr.empty())
		{
			if(!GetLocal(service))
			{
				notCapableReason = "Cannot detect this node's external address. Set the masternodeaddr option in the configuration file.";
				status = MASTERNODE_NOT_CAPABLE;
				
				LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
				
				return;
			}
		}
		else
		{
			service = CService(strMasterNodeAddr, true);
		}

		LogPrintf("CActiveMasternode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString().c_str());


		if(!ConnectNode((CAddress)service, service.ToString().c_str()))
		{
			notCapableReason = "Could not connect to " + service.ToString() + ". Check the masternode's port is open and reachable from the internet.";
			status = MASTERNODE_NOT_CAPABLE;
			
			LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
			
			return;
		}
		
		if(pwalletMain->IsLocked())
		{
			notCapableReason = "Wallet is locked. Unlock the wallet to start the masternode.";
			status = MASTERNODE_NOT_CAPABLE;
			
			LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
			
			return;
		}

		// Set defaults
		status = MASTERNODE_NOT_CAPABLE;
		notCapableReason = "Status could not be determined. Check debug.log for details.";

		// Choose coins to use
		CPubKey pubKeyCollateralAddress;
		CKey keyCollateralAddress;

		// v2.0.0.8: local-start collateral selection now honours
		// masternode.conf (see GetLocalMasternodeVin).  strLocalVinReason
		// carries the specific reason on failure so the not-capable
		// status is informative rather than a generic message.
		std::string strLocalVinReason;
		if(GetLocalMasternodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strLocalVinReason))
		{
			if(GetInputAge(vin) < MASTERNODE_MIN_CONFIRMATIONS)
			{
				notCapableReason = "Collateral input must have at least " +
									boost::lexical_cast<std::string>(MASTERNODE_MIN_CONFIRMATIONS) +
									" confirmations; it currently has " + 
									boost::lexical_cast<std::string>(GetInputAge(vin)) + 
									".";
				
				LogPrintf("CActiveMasternode::ManageStatus() - %s\n", notCapableReason.c_str());
				
				status = MASTERNODE_INPUT_TOO_NEW;
				
				return;
			}

			LogPrintf("CActiveMasternode::ManageStatus() - Is capable master node!\n");

			status = MASTERNODE_IS_CAPABLE;
			notCapableReason = "";

			pwalletMain->LockCoin(vin.prevout);

			// send to all nodes
			CPubKey pubKeyMasternode;
			CKey keyMasternode;

			if(!mnEngineSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode))
			{
				LogPrintf("ActiveMasternode::Dseep() - Error upon calling SetKey: %s\n", errorMessage.c_str());
				
				return;
			}

			/* donations are not supported in DigitalNote.conf */
			CScript donationAddress = CScript();
			int donationPercentage = 0;

			if(!Register(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyMasternode, pubKeyMasternode,
						donationAddress, donationPercentage, errorMessage))
			{
				LogPrintf("CActiveMasternode::ManageStatus() - Error on Register: %s\n", errorMessage.c_str());
			}

			return;
		}
		else
		{
			// GetLocalMasternodeVin sets a specific reason; prefer it.
			notCapableReason = strLocalVinReason.empty()
				? "Could not find a suitable collateral output to start the masternode."
				: strLocalVinReason;
			
			LogPrintf("CActiveMasternode::ManageStatus() - %s\n",
					  notCapableReason.c_str());
		}
	}

	//send to all peers
	if(!Dseep(errorMessage))
	{
		LogPrintf("CActiveMasternode::ManageStatus() - Error on Ping: %s\n", errorMessage.c_str());
	}
}

// Send stop dseep to network for remote masternode
bool CActiveMasternode::StopMasterNode(const std::string &strService, const std::string &strKeyMasternode,
		std::string& errorMessage)
{
	CTxIn vin;
	CKey keyMasternode;
	CPubKey pubKeyMasternode;

	if(!mnEngineSigner.SetKey(strKeyMasternode, errorMessage, keyMasternode, pubKeyMasternode))
	{
		LogPrintf("CActiveMasternode::StopMasterNode() - Error: %s\n", errorMessage.c_str());
		
		return false;
	}

	if (GetMasterNodeVin(vin, pubKeyMasternode, keyMasternode))
	{
		LogPrintf("MasternodeStop::VinFound: %s\n", vin.ToString());
	}

	return StopMasterNode(vin, CService(strService, true), keyMasternode, pubKeyMasternode, errorMessage);
}

// v2.0.0.8 PB-13 fix: stop a specific remote masternode by full identity
// (txhash + outputindex), mirroring Register's signature.  The 3-arg
// StopMasterNode variant above forwards to GetMasterNodeVin without
// txhash/vout, which falls back to possibleCoins[0] in the wallet --
// always picking the first 2M UTXO regardless of which alias the GUI
// asked to stop.  This overload threads through the alias's specific
// collateral so the correct MN is targeted.
bool CActiveMasternode::StopMasterNode(const std::string &strService, const std::string &strKeyMasternode,
		const std::string &strTxHash, const std::string &strOutputIndex,
		std::string& errorMessage)
{
	CTxIn vin;
	CKey keyMasternode;
	CPubKey pubKeyMasternode;

	if(!mnEngineSigner.SetKey(strKeyMasternode, errorMessage, keyMasternode, pubKeyMasternode))
	{
		LogPrintf("CActiveMasternode::StopMasterNode() - Error: %s\n", errorMessage.c_str());

		return false;
	}

	if (!GetMasterNodeVin(vin, pubKeyMasternode, keyMasternode, strTxHash, strOutputIndex))
	{
		errorMessage = "Could not locate vin for txhash " + strTxHash + " vout " + strOutputIndex;
		LogPrintf("CActiveMasternode::StopMasterNode() - Error: %s\n", errorMessage.c_str());

		return false;
	}

	LogPrintf("MasternodeStop::VinFound: %s\n", vin.ToString());

	return StopMasterNode(vin, CService(strService, true), keyMasternode, pubKeyMasternode, errorMessage);
}

// Send stop dseep to network for main masternode
bool CActiveMasternode::StopMasterNode(std::string& errorMessage)
{
	if(status != MASTERNODE_IS_CAPABLE && status != MASTERNODE_REMOTELY_ENABLED)
	{
		errorMessage = "masternode is not in a running status";
		LogPrintf("CActiveMasternode::StopMasterNode() - Error: %s\n", errorMessage.c_str());
		
		return false;
	}

	status = MASTERNODE_STOPPED;

	CPubKey pubKeyMasternode;
	CKey keyMasternode;

	if(!mnEngineSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode))
	{
		LogPrintf("Register::ManageStatus() - Error upon calling SetKey: %s\n", errorMessage.c_str());
		
		return false;
	}

	return StopMasterNode(vin, service, keyMasternode, pubKeyMasternode, errorMessage);
}

// Send stop dseep to network for any masternode
bool CActiveMasternode::StopMasterNode(CTxIn vin, CService service, CKey keyMasternode, CPubKey pubKeyMasternode,
		std::string& errorMessage)
{
	// NOTE: previously this called pwalletMain->UnlockCoin(vin.prevout) here
	// to release the collateral lock when stopping the masternode.  That
	// behavior was wrong: locks are user data, not masternode lifecycle
	// state.  Auto-unlocking on stop silently undid user-set locks
	// (including persistent locks set via the lockunspent RPC or via
	// future GUI lock controls).  The user must now explicitly unlock
	// the collateral via lockunspent true [...] when they actually want
	// to spend it.  The lock on masternode START (in ManageStatus) is
	// retained -- that protects the collateral while the masternode is
	// running, but is no longer rolled back automatically on stop.

	return Dseep(vin, service, keyMasternode, pubKeyMasternode, errorMessage, true);
}

bool CActiveMasternode::Dseep(std::string& errorMessage)
{
	if(status != MASTERNODE_IS_CAPABLE && status != MASTERNODE_REMOTELY_ENABLED)
	{
		errorMessage = "masternode is not in a running status";
		LogPrintf("CActiveMasternode::Dseep() - Error: %s\n", errorMessage.c_str());
		
		return false;
	}

	CPubKey pubKeyMasternode;
	CKey keyMasternode;

	if(!mnEngineSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode))
	{
		LogPrintf("CActiveMasternode::Dseep() - Error upon calling SetKey: %s\n", errorMessage.c_str());
		
		return false;
	}

	return Dseep(vin, service, keyMasternode, pubKeyMasternode, errorMessage, false);
}

bool CActiveMasternode::Dseep(CTxIn vin, CService service, CKey keyMasternode, CPubKey pubKeyMasternode,
		std::string &retErrorMessage, bool stop)
{
	std::string errorMessage;
	std::vector<unsigned char> vchMasterNodeSignature;
	std::string strMasterNodeSignMessage;
	int64_t masterNodeSignatureTime = GetAdjustedTime();

	std::string strMessage = service.ToString() +
							boost::lexical_cast<std::string>(masterNodeSignatureTime) +
							boost::lexical_cast<std::string>(stop);

	if(!mnEngineSigner.SignMessage(strMessage, errorMessage, vchMasterNodeSignature, keyMasternode))
	{
		retErrorMessage = "sign message failed: " + errorMessage;
		LogPrintf("CActiveMasternode::Dseep() - Error: %s\n", retErrorMessage.c_str());
		
		return false;
	}

	if(!mnEngineSigner.VerifyMessage(pubKeyMasternode, vchMasterNodeSignature, strMessage, errorMessage))
	{
		retErrorMessage = "Verify message failed: " + errorMessage;
		LogPrintf("CActiveMasternode::Dseep() - Error: %s\n", retErrorMessage.c_str());
		
		return false;
	}

	// Update Last Seen timestamp in masternode list
	CMasternode* pmn = mnodeman.Find(vin);

	if(pmn != NULL)
	{
		if(stop)
		{
			mnodeman.Remove(pmn->vin);
		}
		else
		{
			pmn->UpdateLastSeen();
		}
	}
	else
	{
		// Seems like we are trying to send a ping while the masternode is not registered in the network
		retErrorMessage = "MNengine Masternode List doesn't include our masternode, Shutting down masternode pinging service! " + vin.ToString();
		LogPrintf("CActiveMasternode::Dseep() - Error: %s\n", retErrorMessage.c_str());
		
		status = MASTERNODE_NOT_CAPABLE;
		notCapableReason = retErrorMessage;
		
		return false;
	}

	//send to all peers
	LogPrintf("CActiveMasternode::Dseep() - RelayMasternodeEntryPing vin = %s\n", vin.ToString().c_str());

	mnodeman.RelayMasternodeEntryPing(vin, vchMasterNodeSignature, masterNodeSignatureTime, stop);

	return true;
}

bool CActiveMasternode::Register(const std::string &strService, const std::string &strKeyMasternode, const std::string &txHash,
		const std::string &strOutputIndex, const std::string &strDonationAddress, const std::string &strDonationPercentage,
		std::string& errorMessage)
{
	CTxIn vin;
	CPubKey pubKeyCollateralAddress;
	CKey keyCollateralAddress;
	CPubKey pubKeyMasternode;
	CKey keyMasternode;
	CScript donationAddress = CScript();
	int donationPercentage = 0;

	if(!mnEngineSigner.SetKey(strKeyMasternode, errorMessage, keyMasternode, pubKeyMasternode))
	{
		LogPrintf("CActiveMasternode::Register() - Error upon calling SetKey: %s\n", errorMessage.c_str());
		
		return false;
	}

	if(!GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, txHash, strOutputIndex)) {
		errorMessage = "could not allocate vin";
		LogPrintf("CActiveMasternode::Register() - Error: %s\n", errorMessage.c_str());
		
		return false;
	}

	CDigitalNoteAddress address;
	if (strDonationAddress != "")
	{
		if(!address.SetString(strDonationAddress))
		{
			LogPrintf("ActiveMasternode::Register - Invalid Donation Address\n");
			
			return false;
		}
		
		donationAddress.SetDestination(address.Get());

		try
		{
			donationPercentage = boost::lexical_cast<int>( strDonationPercentage );
		}
		catch(boost::bad_lexical_cast const&)
		{
			LogPrintf("ActiveMasternode::Register - Invalid Donation Percentage (Couldn't cast)\n");
			
			return false;
		}

		if(donationPercentage < 0 || donationPercentage > 100)
		{
			LogPrintf("ActiveMasternode::Register - Donation Percentage Out Of Range\n");
			
			return false;
		}
	}

	return Register(vin, CService(strService, true), keyCollateralAddress, pubKeyCollateralAddress,
			keyMasternode, pubKeyMasternode, donationAddress, donationPercentage, errorMessage);
}

bool CActiveMasternode::Register(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress,
		CKey keyMasternode, CPubKey pubKeyMasternode, CScript donationAddress, int donationPercentage,
		std::string &retErrorMessage)
{
	std::string errorMessage;
	std::vector<unsigned char> vchMasterNodeSignature;
	std::string strMasterNodeSignMessage;
	int64_t masterNodeSignatureTime = GetAdjustedTime();

	std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
	std::string vchPubKey2(pubKeyMasternode.begin(), pubKeyMasternode.end());

	std::string strMessage = service.ToString() +
							 boost::lexical_cast<std::string>(masterNodeSignatureTime) +
							 vchPubKey +
							 vchPubKey2 +
							 boost::lexical_cast<std::string>(PROTOCOL_VERSION) +
							 donationAddress.ToString() +
							 boost::lexical_cast<std::string>(donationPercentage);

	if(!mnEngineSigner.SignMessage(strMessage, errorMessage, vchMasterNodeSignature, keyCollateralAddress))
	{
		retErrorMessage = "sign message failed: " + errorMessage;
		
		LogPrintf("CActiveMasternode::Register() - Error: %s\n", retErrorMessage.c_str());
		
		return false;
	}

	if(!mnEngineSigner.VerifyMessage(pubKeyCollateralAddress, vchMasterNodeSignature, strMessage, errorMessage)) {
		retErrorMessage = "Verify message failed: " + errorMessage;
		
		LogPrintf("CActiveMasternode::Register() - Error: %s\n", retErrorMessage.c_str());
		
		return false;
	}

	CMasternode* pmn = mnodeman.Find(vin);

	if(pmn == NULL)
	{
		LogPrintf("CActiveMasternode::Register() - Adding to masternode list service: %s - vin: %s\n", service.ToString().c_str(), vin.ToString().c_str());
		
		CMasternode mn(service, vin, pubKeyCollateralAddress, vchMasterNodeSignature, masterNodeSignatureTime, pubKeyMasternode,
			PROTOCOL_VERSION, donationAddress, donationPercentage
		);
		
		mn.ChangeNodeStatus(false);
		mn.UpdateLastSeen(masterNodeSignatureTime);
		
		mnodeman.Add(mn);
	}

	//send to all peers
	LogPrintf("CActiveMasternode::Register() - RelayElectionEntry vin = %s\n", vin.ToString().c_str());

	mnodeman.RelayMasternodeEntry(vin, service, vchMasterNodeSignature, masterNodeSignatureTime, pubKeyCollateralAddress,
		pubKeyMasternode, -1, -1, masterNodeSignatureTime, PROTOCOL_VERSION, donationAddress, donationPercentage
	);

	// Auto-lock the collateral so it isn't accidentally spent or staked
	// while this masternode is active.  ManageStatus() already does this
	// for the local-MN path at the call site that invoked us; remote MNs
	// went unprotected before this fix.  Idempotent if already locked.
	// Fix 1 (AvailableCoinsMN's fIncludeLockedMN bypass) ensures that
	// this lock does not prevent legitimate re-Register operations.
	if (pwalletMain)
	{
		LOCK(pwalletMain->cs_wallet);
		COutPoint prev = vin.prevout;
		if (!pwalletMain->IsLockedCoin(prev.hash, prev.n))
		{
			pwalletMain->LockCoin(prev);
			LogPrintf("CActiveMasternode::Register() - locked collateral %s:%u\n",
			          prev.hash.ToString().c_str(), prev.n);
		}
	}

	return true;
}

bool CActiveMasternode::GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
	return GetMasterNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveMasternode::GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash,
		const std::string &strOutputIndex)
{
	CScript pubScript;

	// Find possible candidates
	std::vector<COutput> possibleCoins = SelectCoinsMasternode();
	COutput *selectedOutput;

	// Find the vin
	if(!strTxHash.empty())
	{
		// Let's find it
		uint256 txHash(strTxHash);
		int outputIndex = boost::lexical_cast<int>(strOutputIndex);
		bool found = false;
		
		for(COutput& out : possibleCoins)
		{
			if(out.tx->GetHash() == txHash && out.i == outputIndex)
			{
				selectedOutput = &out;
				found = true;
				
				break;
			}
		}
		
		if(!found)
		{
			LogPrintf("CActiveMasternode::GetMasterNodeVin - Could not locate valid vin\n");
			
			return false;
		}
	}
	else
	{
		// No output specified,  Select the first one
		if(possibleCoins.size() > 0)
		{
			selectedOutput = &possibleCoins[0];
		}
		else
		{
			LogPrintf("CActiveMasternode::GetMasterNodeVin - Could not locate specified vin from possible list\n");
			
			return false;
		}
	}

	// At this point we have a selected output, retrieve the associated info
	return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}

bool CActiveMasternode::GetMasterNodeVinForPubKey(const std::string &collateralAddress, CTxIn& vin, CPubKey& pubkey,
		CKey& secretKey)
{
	return GetMasterNodeVinForPubKey(collateralAddress, vin, pubkey, secretKey, "", "");
}

bool CActiveMasternode::GetMasterNodeVinForPubKey(const std::string &collateralAddress, CTxIn& vin, CPubKey& pubkey,
		CKey& secretKey, const std::string &strTxHash, const std::string &strOutputIndex)
{
	CScript pubScript;

	// Find possible candidates
	std::vector<COutput> possibleCoins = SelectCoinsMasternodeForPubKey(collateralAddress);
	COutput *selectedOutput;

	// Find the vin
	if(!strTxHash.empty())
	{
		// Let's find it
		uint256 txHash(strTxHash);
		int outputIndex = boost::lexical_cast<int>(strOutputIndex);
		bool found = false;
		
		for(COutput& out : possibleCoins)
		{
			if(out.tx->GetHash() == txHash && out.i == outputIndex)
			{
				selectedOutput = &out;
				found = true;
				
				break;
			}
		}
		
		if(!found)
		{
			LogPrintf("CActiveMasternode::GetMasterNodeVinForPubKey - Could not locate valid vin\n");
			
			return false;
		}
	}
	else
	{
		// No output specified,  Select the first one
		if(possibleCoins.size() > 0)
		{
			selectedOutput = &possibleCoins[0];
		}
		else
		{
			LogPrintf("CActiveMasternode::GetMasterNodeVinForPubKey - Could not locate specified vin from possible list\n");
			
			return false;
		}
	}

	// At this point we have a selected output, retrieve the associated info
	return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}

// Extract masternode vin information from output
bool CActiveMasternode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
	CScript pubScript;

	vin = CTxIn(out.tx->GetHash(),out.i);
	pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

	CTxDestination address1;
	ExtractDestination(pubScript, address1);
	CDigitalNoteAddress address2(address1);

	CKeyID keyID;
	if (!address2.GetKeyID(keyID))
	{
		LogPrintf("CActiveMasternode::GetMasterNodeVin - Address does not refer to a key\n");
		
		return false;
	}

	if (!pwalletMain->GetKey(keyID, secretKey))
	{
		LogPrintf ("CActiveMasternode::GetMasterNodeVin - Private key for address is not known\n");
		
		return false;
	}

	pubkey = secretKey.GetPubKey();

	return true;
}

// v2.0.0.8 -- collateral selection for a LOCALLY-started masternode.
//
// THE BUG this fixes: ManageStatus() (the local-start path) previously
// called the no-txhash GetMasterNodeVin, which selects possibleCoins[0]
// -- the FIRST masternode-collateral UTXO the wallet scan returns --
// with no reference to masternode.conf.  On a host whose wallet holds
// the collateral for several masternodes (the normal cold/collateral
// wallet that funds all of an operator's remotes), a local MN would
// bind to an ARBITRARY collateral, frequently one belonging to a
// declared remote masternode.  Two daemons then run on one collateral
// identity, sign votes with different keys, and the network ban-storms
// on the resulting CheckSignature failures.
//
// THE FIX: masternode.conf is used as a SUBTRACTION FILTER.  The wallet
// scan is kept (it gives autostart and resilience); we only DISAMBIGUATE
// it.  Rules:
//
//   Case A -- a masternode.conf entry exists whose privKey matches THIS
//     daemon's masternodeprivkey (strMasterNodePrivKey).  That entry IS
//     this node's own declaration -> use exactly its txhash/index.
//     Deterministic.
//
//   Case B -- no conf entry matches this daemon's key.  Subtract from the
//     scan every UTXO that matches ANY conf entry (matched on txid AND
//     output index -- a declared collateral belongs to some other,
//     remote masternode), then run the existing "no specific txhash ->
//     first candidate" selection on the REDUCED set.
//
// Refuse to start (return false, reason set) when:
//   Q1 -- the reduced candidate set is empty: every collateral in this
//         wallet is declared in masternode.conf as a (remote) masternode,
//         so this node has no collateral of its own.
//   Q2 -- masternode.conf exists but a line will not parse: the filter
//         cannot be trusted, so refuse rather than risk binding to a
//         remote's collateral.  (A conf that is simply ABSENT is fine --
//         nothing to subtract, Case B proceeds on the full scan.)
//
// masternode.conf is re-read and re-validated HERE rather than relying on
// the init-time CMasternodeConfig::read() -- that call's bool result is
// discarded by init.cpp, and a parse failure there leaves a PARTIALLY
// populated entries vector (parsing stops at the bad line).  A partial
// list would under-subtract.  The file is tiny; re-parsing once per MN
// start is free.
bool CActiveMasternode::GetLocalMasternodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey,
		std::string& strNotCapableReason)
{
	// --- Re-read + validate masternode.conf (Q2) ------------------------
	// entryTxes: (txid, outputIndex) of every conf entry.
	// myConfTx : the conf entry matching THIS daemon's key, if any (Case A).
	std::vector<std::pair<uint256, int> > entryTxes;
	bool haveMyConfEntry = false;
	uint256 myConfTxHash = 0;
	int myConfOutputIndex = 0;

	{
		boost::filesystem::ifstream streamConfig(GetMasternodeConfigFile());

		if (streamConfig.good())
		{
			for (std::string line; std::getline(streamConfig, line); )
			{
				if (line.empty())
				{
					continue;
				}

				std::istringstream iss(line);
				std::string cAlias, cIp, cPrivKey, cTxHash, cOutputIndex;

				if (!(iss >> cAlias >> cIp >> cPrivKey >> cTxHash >> cOutputIndex))
				{
					// Q2: malformed line -> filter untrustworthy -> refuse.
					streamConfig.close();
					strNotCapableReason =
						"Cannot start: masternode.conf could not be parsed "
						"(bad line: \"" + line + "\"). Fix the file -- starting "
						"could otherwise select a collateral belonging to "
						"another masternode.";
					LogPrintf("CActiveMasternode::GetLocalMasternodeVin - %s\n",
							  strNotCapableReason.c_str());
					return false;
				}

				uint256 cHash(cTxHash);
				int cIndex = 0;
				try
				{
					cIndex = boost::lexical_cast<int>(cOutputIndex);
				}
				catch (boost::bad_lexical_cast &)
				{
					streamConfig.close();
					strNotCapableReason =
						"Cannot start: masternode.conf has a non-numeric "
						"output index (line: \"" + line + "\"). Fix the file "
						"before starting.";
					LogPrintf("CActiveMasternode::GetLocalMasternodeVin - %s\n",
							  strNotCapableReason.c_str());
					return false;
				}

				entryTxes.push_back(std::make_pair(cHash, cIndex));

				// Case A test: this conf entry IS this daemon if its privKey
				// matches the running masternodeprivkey.
				if (cPrivKey == strMasterNodePrivKey)
				{
					haveMyConfEntry = true;
					myConfTxHash = cHash;
					myConfOutputIndex = cIndex;
				}
			}

			streamConfig.close();
		}
		// streamConfig not good == no masternode.conf == fine (Case B,
		// nothing to subtract).
	}

	// --- The wallet scan (unchanged -- all 2M-XDN collateral UTXOs) -----
	std::vector<COutput> possibleCoins = SelectCoinsMasternode();

	if (possibleCoins.empty())
	{
		strNotCapableReason =
			"No " +
			boost::lexical_cast<std::string>(MasternodeCollateral(pindexBest->nHeight)) +
			" XDN collateral output found in this wallet. If this masternode "
			"is started remotely (collateral held in a separate wallet), this "
			"is expected -- it will activate when the controlling wallet "
			"broadcasts the start. If it is meant to start locally, the "
			"collateral must be present in this wallet.";
		LogPrintf("CActiveMasternode::GetLocalMasternodeVin - %s\n",
				  strNotCapableReason.c_str());
		return false;
	}

	// --- Case A: this daemon has its own masternode.conf entry ----------
	if (haveMyConfEntry)
	{
		for (COutput& out : possibleCoins)
		{
			if (out.tx->GetHash() == myConfTxHash && out.i == myConfOutputIndex)
			{
				LogPrintf("CActiveMasternode::GetLocalMasternodeVin - using "
						  "masternode.conf collateral %s:%d for this node\n",
						  myConfTxHash.ToString().c_str(), myConfOutputIndex);
				return GetVinFromOutput(out, vin, pubkey, secretKey);
			}
		}

		// Conf names a collateral for this node, but the wallet does not
		// hold it -> cannot start as that masternode.  Refuse rather than
		// silently fall through to picking some other collateral.
		strNotCapableReason =
			"masternode.conf names collateral " + myConfTxHash.ToString() +
			" for this node, but that output is not present in this wallet.";
		LogPrintf("CActiveMasternode::GetLocalMasternodeVin - %s\n",
				  strNotCapableReason.c_str());
		return false;
	}

	// --- Case B: subtract every conf-declared collateral ----------------
	// What remains is collateral NOT spoken for by a declared masternode.
	std::vector<COutput> filteredCoins;

	for (COutput& out : possibleCoins)
	{
		bool declaredElsewhere = false;

		for (unsigned int j = 0; j < entryTxes.size(); j++)
		{
			// Q3: match on txid AND output index.
			if (out.tx->GetHash() == entryTxes[j].first &&
				out.i == entryTxes[j].second)
			{
				declaredElsewhere = true;
				break;
			}
		}

		if (!declaredElsewhere)
		{
			filteredCoins.push_back(out);
		}
	}

	// Q1: nothing left after subtraction -> refuse.
	if (filteredCoins.empty())
	{
		strNotCapableReason =
			"Every " +
			boost::lexical_cast<std::string>(MasternodeCollateral(pindexBest->nHeight)) +
			" XDN collateral in this wallet is already declared in "
			"masternode.conf for another masternode. This node has no "
			"collateral of its own to start a local masternode.";
		LogPrintf("CActiveMasternode::GetLocalMasternodeVin - %s\n",
				  strNotCapableReason.c_str());
		return false;
	}

	// Existing behaviour, but on the safe (reduced) set: take the first.
	if (filteredCoins.size() > 1)
	{
		LogPrintf("CActiveMasternode::GetLocalMasternodeVin - %u candidate "
				  "collaterals after masternode.conf filtering; using the "
				  "first. Add this node to masternode.conf to pin it.\n",
				  (unsigned int)filteredCoins.size());
	}

	return GetVinFromOutput(filteredCoins[0], vin, pubkey, secretKey);
}

// get all possible outputs for running masternode
std::vector<COutput> CActiveMasternode::SelectCoinsMasternode()
{
	std::vector<COutput> vCoins;
	std::vector<COutput> filteredCoins;

	// Retrieve all possible outputs.  We pass fIncludeLockedMN=true so
	// that locked outputs ARE candidates for masternode start.  Locks
	// are user data: they prevent accidental spends/stakes, but they
	// must not block the legitimate "use this collateral for a masternode"
	// operation.  Without this, MN restart fails with "could not allocate
	// vin" whenever the collateral is locked -- which is the recommended
	// state for any active MN's collateral.
	pwalletMain->AvailableCoinsMN(vCoins, true, NULL, ALL_COINS, false, true);

	// Filter
	for(const COutput& out : vCoins)
	{
		//exactly
		if(out.tx->vout[out.i].nValue == MasternodeCollateral(pindexBest->nHeight)*COIN)
		{
			filteredCoins.push_back(out);
		}
	}

	return filteredCoins;
}

// get all possible outputs for running masternode for a specific pubkey
std::vector<COutput> CActiveMasternode::SelectCoinsMasternodeForPubKey(const std::string &collateralAddress)
{
	CDigitalNoteAddress address(collateralAddress);
	CScript scriptPubKey;
	scriptPubKey.SetDestination(address.Get());
	std::vector<COutput> vCoins;
	std::vector<COutput> filteredCoins;

	// Retrieve all possible outputs
	pwalletMain->AvailableCoins(vCoins);

	// Filter
	for(const COutput& out : vCoins)
	{
		 //exactly
		if(out.tx->vout[out.i].scriptPubKey == scriptPubKey && out.tx->vout[out.i].nValue == MasternodeCollateral(pindexBest->nHeight)*COIN)
		{
			filteredCoins.push_back(out);
		}
	}

	return filteredCoins;
}

// when starting a masternode, this can enable to run as a hot wallet with no funds
bool CActiveMasternode::EnableHotColdMasterNode(CTxIn& newVin, CService& newService)
{
	if(!fMasterNode)
	{
		return false;
	}

	status = MASTERNODE_REMOTELY_ENABLED;
	notCapableReason = "";

	//The values below are needed for signing dseep messages going forward
	this->vin = newVin;
	this->service = newService;

	LogPrintf("CActiveMasternode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

	return true;
}
// ===========================================================================

// ===========================================================================
// v2.0.0.8 M1Q -- BroadcastQueue and the deterministic queue simulation.
//
// Replaces the per-height BroadcastVote post-activation.  Instead of voting a
// single payee for one future height, an MN broadcasts an ORDERED QUEUE of
// the next VOTE_QUEUE_LENGTH payees, computed by forward-simulating the
// rotation.  Because the simulation is a pure function of chain-derived state
// (collateral confirm heights + mapLastPaidHeight), every honest MN computes
// the identical queue, so per-position consensus is trivially full and the
// payment streak (ledger S18) cannot occur: position p+1 is, by construction,
// the MN that rotates in AFTER position p's winner.
//
// See v208-M1Q-queue-based-voting-SPEC.md S5 (computation), S18.1 (snapshot
// under one lock), S18.4 (pure function, explicit nQueueHeight).
// ===========================================================================

namespace {

// Pure forward simulation -- no locks, no globals.  Mirrors
// FindOldestNotInVecChainDerived's ranking exactly: eligibility via
// IsVotingEligible(referenceHeight) reconstructed from confirmedHeight; rank
// by paidHeight (or confirmedHeight if never paid, else 0); smallest wins,
// tiebreak on smallest vin.  After each position is filled, the chosen MN's
// simulated paidHeight is advanced to that position's target height, so the
// next position sees it as freshly paid and rotates past it.
//
// Operates on the CMnPaymentSnapshotEntry vector captured by
// mnodeman.GetQueuePaymentSnapshot() under a single lock (spec S18.1).
//
// referenceHeight for position p mirrors BroadcastVote's anchor:
//   targetHeight   = nQueueHeight + 1 + p
//   referenceHeight = targetHeight - VOTE_LOOKAHEAD - REORG_DEPTH_BUFFER
// identical for every node computing the same queue.
std::vector<CScript> SimulateQueue(int nQueueHeight,
                                   const std::vector<CMnPaymentSnapshotEntry> &candidatesIn)
{
	std::vector<CScript> result;

	// Local, mutable copy of paid-heights for the simulation.  Keyed by vin.
	std::map<COutPoint, int> simPaid;
	for (std::vector<CMnPaymentSnapshotEntry>::const_iterator it = candidatesIn.begin();
	     it != candidatesIn.end(); ++it)
	{
		if (it->hasPaid)
		{
			simPaid[it->vin] = it->paidHeight;
		}
	}

	for (int p = 0; p < VOTE_QUEUE_LENGTH; ++p)
	{
		int targetHeight = nQueueHeight + 1 + p;
		int referenceHeight = targetHeight - VOTE_LOOKAHEAD - REORG_DEPTH_BUFFER;

		const CMnPaymentSnapshotEntry *best = NULL;
		int bestRank = 0;

		for (std::vector<CMnPaymentSnapshotEntry>::const_iterator it = candidatesIn.begin();
		     it != candidatesIn.end(); ++it)
		{
			// Eligibility: IsVotingEligible(referenceHeight), reconstructed.
			if (referenceHeight <= 0)
			{
				continue;
			}
			if (it->confirmedHeight < 0)
			{
				continue;
			}
			if ((it->confirmedHeight + VOTER_ELIGIBILITY_DEPTH) > referenceHeight)
			{
				continue;
			}

			// Ranking value: simulated paid height if present, else the
			// confirm-height fallback (never 0 for an eligible MN, since
			// eligibility required confirmedHeight >= 0).
			int rank;
			std::map<COutPoint, int>::const_iterator sit = simPaid.find(it->vin);
			if (sit != simPaid.end())
			{
				rank = sit->second;
			}
			else
			{
				rank = (it->confirmedHeight >= 0) ? it->confirmedHeight : 0;
			}

			bool better = false;
			if (best == NULL)
			{
				better = true;
			}
			else if (rank < bestRank)
			{
				better = true;
			}
			else if (rank == bestRank && it->vin < best->vin)
			{
				better = true;
			}

			if (better)
			{
				best = &(*it);
				bestRank = rank;
			}
		}

		if (best == NULL)
		{
			// No eligible candidate for this position.  Emit an empty script;
			// the receiver's per-position tally will simply not reach
			// consensus on an empty payee, and GetEnforcedPayee falls back to
			// legacy for that height.  (In practice this only happens very
			// early in the chain, where referenceHeight <= 0.)
			result.push_back(CScript());
			continue;
		}

		result.push_back(best->payeeScript);

		// Advance the chosen MN's simulated paid height to this target so the
		// next position rotates past it -- THIS is what breaks the streak.
		simPaid[best->vin] = targetHeight;
	}

	return result;
}

} // anonymous namespace

bool CActiveMasternode::BroadcastQueue(int nQueueHeight)
{
	// Gates 1-3: identical to BroadcastVote.
	if (strMasterNodePrivKey.empty())
	{
		return false;
	}

	if (status != MASTERNODE_IS_CAPABLE && status != MASTERNODE_REMOTELY_ENABLED)
	{
		if (fDebug)
		{
			LogPrintf("CActiveMasternode::BroadcastQueue -- skipping: status %d not capable/enabled\n",
			          status);
		}

		return false;
	}

	if (pindexBest == NULL)
	{
		return false;
	}

	// Snapshot all candidate MN state via the manager's purpose-built
	// accessor, which captures everything under a SINGLE cs acquisition
	// (spec S18.1).  mnodeman.cs is private; external code never holds it
	// directly -- GetQueuePaymentSnapshot is the encapsulated entry point.
	// The snapshot captures exactly the inputs FindOldestNotInVecChainDerived
	// reads: collateral confirm height (eligibility + never-paid fallback)
	// and mapLastPaidHeight (ranking).
	std::vector<CMnPaymentSnapshotEntry> candidates = mnodeman.GetQueuePaymentSnapshot();

	// Pure simulation -- no lock held.
	std::vector<CScript> queue = SimulateQueue(nQueueHeight, candidates);

	if ((int)queue.size() != VOTE_QUEUE_LENGTH)
	{
		// Should never happen -- SimulateQueue always emits exactly
		// VOTE_QUEUE_LENGTH entries -- but guard the invariant the wire
		// format and receiver depend on.
		LogPrintf("CActiveMasternode::BroadcastQueue -- internal error: simulated "
		          "queue length %d != VOTE_QUEUE_LENGTH %d\n",
		          (int)queue.size(), VOTE_QUEUE_LENGTH);
		return false;
	}

	// Build and sign the queue.
	CMasternodeVoteQueue q(vin, nQueueHeight, queue);

	if (!q.Sign(strMasterNodePrivKey))
	{
		LogPrintf("CActiveMasternode::BroadcastQueue -- Sign failed for nQueueHeight %d\n",
		          nQueueHeight);
		return false;
	}

	LogPrint("masternode", "CActiveMasternode::BroadcastQueue -- broadcasting queue from MN %s for "
	          "nQueueHeight %d (%d positions)\n",
	          vin.prevout.ToString(), nQueueHeight, (int)queue.size());

	// Process our own queue locally before broadcasting (same rationale as
	// BroadcastVote's self-ProcessVote: keep our local tally consistent with
	// the network's, and don't under-count our own contribution).
	voteTracker.ProcessQueue(q, NULL);

	// Push to every connected peer.  Pre-M1Q peers silently drop the unknown
	// "mnvotequeue" command.
	{
		LOCK(cs_vNodes);

		for (CNode *pnode : vNodes)
		{
			pnode->PushMessage("mnvotequeue", q);
		}
	}

	return true;
}
