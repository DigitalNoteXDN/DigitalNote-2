#ifndef CACTIVEMASTERNODE_H
#define CACTIVEMASTERNODE_H

#include <string>
#include <vector>

#include "ctxin.h"
#include "net/cservice.h"
#include "cpubkey.h"

class COutput;
class CKey;
class CScript;

// Responsible for activating the masternode and pinging the network
class CActiveMasternode
{
public:
	CPubKey pubKeyMasternode;
	CTxIn vin;
	CService service;
	int status;
	std::string notCapableReason;

	CActiveMasternode();

	void ManageStatus();
	bool Dseep(std::string& errorMessage);
	bool Dseep(CTxIn vin, CService service, CKey key, CPubKey pubKey, std::string &retErrorMessage, bool stop);
	bool StopMasterNode(std::string& errorMessage);																		// stop main masternode
	bool StopMasterNode(const std::string &strService, const std::string &strKeyMasternode, std::string& errorMessage); // stop remote masternode
	// v2.0.0.8 PB-13 fix: stop a specific remote masternode by full identity
	// (matches Register's signature).  The 3-arg variant above falls back to
	// possibleCoins[0] when locating the vin, which always picks the first
	// 2M UTXO in the wallet regardless of which alias was requested.  Use
	// this overload from the GUI worker so the correct MN is targeted.
	bool StopMasterNode(const std::string &strService, const std::string &strKeyMasternode,
			const std::string &strTxHash, const std::string &strOutputIndex,
			std::string& errorMessage);
	bool StopMasterNode(CTxIn vin, CService service, CKey key, CPubKey pubKey, std::string& errorMessage);				// stop any masternode

	/// Register remote Masternode
	bool Register(const std::string &strService, const std::string &strKey, const std::string &txHash, const std::string &strOutputIndex,
			const std::string &strDonationAddress, const std::string &strDonationPercentage, std::string& errorMessage); 
	/// Register any Masternode
	bool Register(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyMasternode, CPubKey pubKeyMasternode,
			CScript donationAddress, int donationPercentage, std::string &retErrorMessage);  

	// get 2,000,000 XDN input that can be used for the masternode
	bool GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
	bool GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, const std::string &strOutputIndex);
	bool GetMasterNodeVinForPubKey(const std::string &collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
	bool GetMasterNodeVinForPubKey(const std::string &collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey, const std::string &strTxHash,
			const std::string &strOutputIndex);
	std::vector<COutput> SelectCoinsMasternode();
	std::vector<COutput> SelectCoinsMasternodeForPubKey(const std::string &collateralAddress);
	bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

	// v2.0.0.8: collateral selection for a LOCALLY-started masternode.
	// Honours masternode.conf so a local MN cannot bind to a collateral
	// that belongs to another (remote) masternode declared in the conf.
	// Returns false (MN must not start) on an ambiguous / empty / invalid
	// situation; see the implementation for the exact rules.
	bool GetLocalMasternodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey,
			std::string& strNotCapableReason);

	// enable hot wallet mode (run a masternode with no funds)
	bool EnableHotColdMasterNode(CTxIn& vin, CService& addr);

	// v2.0.0.8 M1Q: compute and broadcast a full ordered payee queue for the
	// next VOTE_QUEUE_LENGTH heights, computed by deterministic forward
	// simulation of the rotation from nQueueHeight.  Replaces BroadcastVote
	// post-activation.  Called on every block-connect from main.cpp's
	// ProcessBlock tip-update path.  Returns false (harmlessly) on the same
	// gate conditions as BroadcastVote.  See
	// v208-M1Q-queue-based-voting-SPEC.md S5/S11.
	bool BroadcastQueue(int nQueueHeight);
};

#endif // CACTIVEMASTERNODE_H
