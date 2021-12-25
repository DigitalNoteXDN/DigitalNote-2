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

    // enable hot wallet mode (run a masternode with no funds)
    bool EnableHotColdMasterNode(CTxIn& vin, CService& addr);
};

#endif // CACTIVEMASTERNODE_H
