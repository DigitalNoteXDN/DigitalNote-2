#ifndef CMNENGINEPOOL_H
#define CMNENGINEPOOL_H

#include <string>
#include <vector>

#include "types/ccriticalsection.h"
#include "ctransaction.h"
#include "cscript.h"

class CMNengineEntry;
class CMasternode;
class CTxIn;
class CTxOut;
class CTxDSIn;
class CTxDSOut;
class CNode;

/** Used to keep track of current status of MNengine pool
 */
class CMNenginePool
{
private:
	mutable CCriticalSection cs_mnengine;

	std::vector<CMNengineEntry> entries;		// Masternode entries
	CTransaction finalTransaction;				// the finalized transaction ready for signing
	int64_t lastTimeChanged;					// last time the 'state' changed, in UTC milliseconds
	unsigned int state;							// should be one of the POOL_STATUS_XXX values
	unsigned int entriesCount;
	unsigned int lastEntryAccepted;
	unsigned int countEntriesAccepted;
	std::vector<CTxIn> lockedCoins;
	std::string lastMessage;
	bool unitTest;
	int sessionID;
	int sessionUsers;							//N Users have said they'll join
	bool sessionFoundMasternode;				//If we've found a compatible Masternode
	std::vector<CTransaction> vecSessionCollateral;
	int cachedLastSuccess;
	int minBlockSpacing;						//required blocks between mixes
	CTransaction txCollateral;
	int64_t lastNewBlock;

public:
	enum messages {
		ERR_ALREADY_HAVE,
		ERR_ENTRIES_FULL,
		ERR_EXISTING_TX,
		ERR_FEES,
		ERR_INVALID_COLLATERAL,
		ERR_INVALID_INPUT,
		ERR_INVALID_SCRIPT,
		ERR_INVALID_TX,
		ERR_MAXIMUM,
		ERR_MN_LIST,
		ERR_MODE,
		ERR_NON_STANDARD_PUBKEY,
		ERR_NOT_A_MN,
		ERR_QUEUE_FULL,
		ERR_RECENT,
		ERR_SESSION,
		ERR_MISSING_TX,
		ERR_VERSION,
		MSG_NOERR,
		MSG_SUCCESS,
		MSG_ENTRIES_ADDED
	};

	CScript collateralPubKey;					// where collateral should be made out to
	CMasternode* pSubmittedToMasternode;
	int cachedNumBlocks;						// used for the overview screen

	CMNenginePool();

	void InitCollateralAddress();
	void SetMinBlockSpacing(int minBlockSpacingIn);
	bool SetCollateralAddress(const std::string &strAddress);
	void Reset();
	void SetNull();
	void UnlockCoins();
	bool IsNull() const;
	int GetState() const;
	std::string GetStatus();
	int GetEntriesCount() const;
	int GetLastEntryAccepted() const;
	int GetCountEntriesAccepted() const;
	void UpdateState(unsigned int newState);
	int GetMaxPoolTransactions();
	bool IsSessionReady();
	bool IsBlockchainSynced();
	void Check();
	void CheckFinalTransaction();
	/// Charge fees to bad actors (Charge clients a fee if they're abusive)
	void ChargeFees();
	/// Rarely charge fees to pay miners
	void ChargeRandomFees();
	void CheckTimeout();
	void CheckForCompleteQueue();
	/// Check to make sure a signature matches an input in the pool
	bool SignatureValid(const CScript& newSig, const CTxIn& newVin);
	/// If the collateral is valid given by a client
	bool IsCollateralValid(const CTransaction& txCollateral);
	/// Add a clients entry to the pool
	bool AddEntry(const std::vector<CTxIn>& newInput, const int64_t& nAmount, const CTransaction& txCollateral,
		const std::vector<CTxOut>& newOutput, std::string& error);
	/// Add signature to a vin
	bool AddScriptSig(const CTxIn& newVin);
	/// Check that all inputs are signed. (Are all inputs signed?)
	bool SignaturesComplete();
	/// Get Masternode updates about the progress of MNengine
	bool StatusUpdate(int newState, int newEntriesCount, int newAccepted, std::string& error, int newSessionID=0);

	/// As a client, check and sign the final transaction
	bool SignFinalTransaction(CTransaction& finalTransactionNew, CNode* node);

	/// Get the last valid block hash for a given modulus
	bool GetLastValidBlockHash(uint256& hash, int mod=1, int nBlockHeight=0);
	/// Process a new block
	void NewBlock();
	void CompletedTransaction(bool error, int errorID);
	void ClearLastMessage();
	/// Used for liquidity providers
	bool SendRandomPaymentToSelf();

	/// Split up large inputs or make fee sized inputs
	bool MakeCollateralAmounts();

	std::string GetMessageByID(int messageID);

	//
	// Relay MNengine Messages
	//
	void RelayFinalTransaction(const int sessionID, const CTransaction& txNew);
	void RelaySignaturesAnon(std::vector<CTxIn>& vin);
	void RelayInAnon(std::vector<CTxIn>& vin, std::vector<CTxOut>& vout);
	void RelayIn(const std::vector<CTxDSIn>& vin, const int64_t& nAmount, const CTransaction& txCollateral, const std::vector<CTxDSOut>& vout);
	void RelayStatus(const int sessionID, const int newState, const int newEntriesCount, const int newAccepted, const std::string &error="");
	void RelayCompletedTransaction(const int sessionID, const bool error, const std::string &errorMessage);
};

#endif // CMNENGINEPOOL_H
