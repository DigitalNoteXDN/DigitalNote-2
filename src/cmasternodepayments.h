#ifndef CMASTERNODEPAYMENTS_H
#define CMASTERNODEPAYMENTS_H

#include <vector>
#include <string>

class CMasternodePaymentWinner;
class uint256;
class CTxIn;
class CNode;
class CMasternode;
class CScript;

//
// Masternode Payments Class
// Keeps track of who should get paid for which blocks
//

class CMasternodePayments
{
private:
	std::vector<CMasternodePaymentWinner> vWinning;
	int nSyncedFromPeer;
	std::string strMasterPrivKey;
	std::string strMainPubKey;
	bool enabled;
	int nLastBlockHeight;

public:
	CMasternodePayments();

	bool SetPrivKey(const std::string &strPrivKey);
	bool CheckSignature(CMasternodePaymentWinner& winner);
	bool Sign(CMasternodePaymentWinner& winner);

	// Deterministically calculate a given "score" for a masternode depending on how close it's hash is
	// to the blockHeight. The further away they are the better, the furthest will win the election
	// and get paid this block
	//

	uint64_t CalculateScore(uint256 blockHash, CTxIn& vin);
	bool GetWinningMasternode(int nBlockHeight, CTxIn& vinOut);
	bool AddWinningMasternode(CMasternodePaymentWinner& winner);
	bool ProcessBlock(int nBlockHeight);
	void Relay(CMasternodePaymentWinner& winner);
	void Sync(CNode* node);
	void CleanPaymentList();
	int LastPayment(CMasternode& mn);
	int GetMinMasternodePaymentsProto();

	bool GetBlockPayee(int nBlockHeight, CScript& payee, CTxIn& vin);
};

#endif // CMASTERNODEPAYMENTS_H
