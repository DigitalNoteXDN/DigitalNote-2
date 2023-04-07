#ifndef CMASTERNODEMAN_H
#define CMASTERNODEMAN_H

#include <vector>
#include <map>

#include "types/ccriticalsection.h"

class CNode;
class CMasternode;
class CNetAddr;
class COutPoint;
class CService;
class CTxIn;
class CPubKey;
class CDataStream;
class CScript;

class CMasternodeMan
{
private:
	// critical section to protect the inner data structures
	mutable CCriticalSection cs;

	// map to hold all MNs
	std::vector<CMasternode> vMasternodes;
	// who's asked for the masternode list and the last time
	std::map<CNetAddr, int64_t> mAskedUsForMasternodeList;
	// who we asked for the masternode list and the last time
	std::map<CNetAddr, int64_t> mWeAskedForMasternodeList;
	// which masternodes we've asked for
	std::map<COutPoint, int64_t> mWeAskedForMasternodeListEntry;

public:
	// keep track of dsq count to prevent masternodes from gaming mnengine queue
	int64_t nDsqCount;

	CMasternodeMan();
	CMasternodeMan(CMasternodeMan& other);

	// Add an entry
	bool Add(CMasternode &mn);

	// Check all masternodes
	void Check();

	/// Ask (source) node for mnb
	void AskForMN(CNode* pnode, CTxIn &vin);

	// Check all masternodes and remove inactive
	void CheckAndRemove();

	// Clear masternode vector
	void Clear();

	int CountEnabled(int protocolVersion = -1);

	int CountMasternodesAboveProtocol(int protocolVersion);

	void DsegUpdate(CNode* pnode);

	// Find an entry
	CMasternode* Find(const CTxIn& vin);
	CMasternode* Find(const CPubKey& pubKeyMasternode);

	//Find an entry thta do not match every entry provided vector
	CMasternode* FindOldestNotInVec(const std::vector<CTxIn> &vVins, int nMinimumAge);

	// Find a random entry
	CMasternode* FindRandom();

	/// Find a random entry
	CMasternode* FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion = -1);

	// Get the current winner for this block
	CMasternode* GetCurrentMasterNode(int mod=1, int64_t nBlockHeight=0, int minProtocol=0);

	bool IsPayeeAValidMasternode(CScript payee);
	std::vector<CMasternode> GetFullMasternodeVector();

	std::vector<std::pair<int, CMasternode>> GetMasternodeRanks(int64_t nBlockHeight, int minProtocol=0);
	int GetMasternodeRank(const CTxIn &vin, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);
	CMasternode* GetMasternodeByRank(int nRank, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);

	void ProcessMasternodeConnections();

	void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

	std::string ToString() const;
	int size();

	//
	// Relay Masternode Messages
	//

	void RelayMasternodeEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey,
			const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion, CScript donationAddress, int donationPercentage);
	void RelayMasternodeEntryPing(const CTxIn vin, const std::vector<unsigned char> vchSig, const int64_t nNow, const bool stop);

	void Remove(CTxIn vin);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CMASTERNODEMAN_H
