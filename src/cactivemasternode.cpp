#include "compat.h"

#include <boost/lexical_cast.hpp>

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
#include "masternode.h"
#include "masternodeman.h"
#include "masternode_extern.h"
#include "ctxout.h"
#include "cmnenginesigner.h"
#include "mnengine_extern.h"
#include "init.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "net/cnode.h"
#include "cmasternodevote.h"
#include "version.h"

#include "cactivemasternode.h"

CActiveMasternode::CActiveMasternode()
{
	status = MASTERNODE_NOT_PROCESSED;
}

//
// Bootup the masternode, look for a 2,000,000 XDN input and register on the network
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
				notCapableReason = "Can't detect external address. Please use the masternodeaddr configuration option.";
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
			notCapableReason = "Could not connect to " + service.ToString();
			status = MASTERNODE_NOT_CAPABLE;
			
			LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
			
			return;
		}
		
		if(pwalletMain->IsLocked())
		{
			notCapableReason = "Wallet is locked.";
			status = MASTERNODE_NOT_CAPABLE;
			
			LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
			
			return;
		}

		// Set defaults
		status = MASTERNODE_NOT_CAPABLE;
		notCapableReason = "Unknown. Check debug.log for more information.\n";

		// Choose coins to use
		CPubKey pubKeyCollateralAddress;
		CKey keyCollateralAddress;

		if(GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress))
		{
			if(GetInputAge(vin) < MASTERNODE_MIN_CONFIRMATIONS)
			{
				notCapableReason = "Input must have least " +
									boost::lexical_cast<std::string>(MASTERNODE_MIN_CONFIRMATIONS) +
									" confirmations - " + 
									boost::lexical_cast<std::string>(GetInputAge(vin)) + 
									" confirmations";
				
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
			notCapableReason = "Could not find suitable coins!";
			
			LogPrintf("CActiveMasternode::ManageStatus() - Could not find suitable coins!\n");
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
// v2.0.0.8 M2: BroadcastVote
//
// Hot wallets running with -masternode=1 call this on every block-connect
// from main.cpp's ProcessBlock tip-update path.
// ===========================================================================

bool CActiveMasternode::BroadcastVote(int forHeight)
{
	// Gate 1: must be running as an active MN.  Non-MN wallets have an
	// empty strMasterNodePrivKey and can't sign.  Without -masternode=1
	// this path is never reached anyway (caller in main.cpp checks
	// fMasterNode), but defensive belt-and-braces.
	if (strMasterNodePrivKey.empty())
	{
		return false;
	}

	// Gate 2: must be operationally enabled.  An MN that's still in
	// MASTERNODE_SYNC_IN_PROCESS or MASTERNODE_NOT_CAPABLE shouldn't yet
	// produce votes -- other peers would reject them as unverifiable (no
	// matching pubkey2 in their MN list).
	if (status != MASTERNODE_IS_CAPABLE && status != MASTERNODE_REMOTELY_ENABLED)
	{
		if (fDebug)
		{
			LogPrintf("CActiveMasternode::BroadcastVote -- skipping: status %d not capable/enabled\n",
					  status);
		}

		return false;
	}

	// Gate 3: chain state must be sane.
	if (pindexBest == NULL)
	{
		return false;
	}

	// Compute the canonical winner using chain-derived data with reorg
	// protection.  Reference height is (currentTip - REORG_DEPTH_BUFFER) so
	// votes are stable against typical 1-block reorgs.
	int referenceHeight = pindexBest->nHeight - REORG_DEPTH_BUFFER;

	if (referenceHeight < 0)
	{
		// Very early chain; nothing meaningful to vote on yet.
		return false;
	}

	CMasternode *winner = mnodeman.FindOldestNotInVecChainDerived(
			std::vector<CTxIn>(), 0, referenceHeight);

	if (winner == NULL)
	{
		if (fDebug)
		{
			LogPrintf("CActiveMasternode::BroadcastVote -- no candidate winner for height %d\n",
					  forHeight);
		}

		return false;
	}

	CScript payeeScript = GetScriptForDestination(winner->pubkey.GetID());

	// Build and sign the vote.
	CMasternodeVote vote(vin, forHeight, payeeScript);

	if (!vote.Sign(strMasterNodePrivKey))
	{
		LogPrintf("CActiveMasternode::BroadcastVote -- Sign failed for height %d\n", forHeight);

		return false;
	}

	LogPrintf("CActiveMasternode::BroadcastVote -- voting MN %s for height %d (winner %s)\n",
			  vin.prevout.ToString(), forHeight, CDigitalNoteAddress(winner->pubkey.GetID()).ToString());

	// v2.0.0.8 M3 patch 5: process our own vote locally before broadcasting.
	// Without this, our own getvoteinfo under-counts by 1 (we never see our
	// own vote arrive as a network message), and during M4 enforcement our
	// local GetCanonicalWinner could disagree with the network's view.
	//
	// ProcessVote is the same code path used for network-received votes:
	// it does signature-equivalent gating, dedup, equivocation detection,
	// and aggregation.  Calling with pfrom=NULL is the documented contract
	// for self-originated votes.
	voteTracker.ProcessVote(vote, NULL);

	// Push to every connected peer.  v2.0.0.7 peers will silently drop
	// the unknown "mnvote" command; v2.0.0.8+ peers process it.
	{
		LOCK(cs_vNodes);

		for (CNode *pnode : vNodes)
		{
			pnode->PushMessage("mnvote", vote);
		}
	}

	return true;
}
