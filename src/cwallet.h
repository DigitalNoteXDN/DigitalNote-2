#ifndef CWALLET_H
#define CWALLET_H

#include <set>
#include <map>

#include "cwalletinterface.h"
#include "ccryptokeystore.h"
#include "cpubkey.h"
#include "types/mapvalue_t.h"
#include "types/txitems.h"
#include "types/isminefilter.h"
#include "types/camount.h"
#include "enums/changetype.h"
#include "enums/isminetype.h"
#include "enums/dberrors.h"

class CWalletDB;
class CWalletTx;
class COutPoint;
class uint256;
class CCoinControl;
class CAccountingEntry;
class COutput;
class CDigitalNoteSecret;
class CKeyPool;
class CMasterKey;
class CKeyMetadata;
class CStealthKeyMetadata;

typedef std::map<CKeyID, CStealthKeyMetadata> StealthKeyMetaMap;

/** (client) version numbers for particular wallet features */
enum WalletFeature
{
    FEATURE_BASE = 10500, // the earliest version new wallets supports (only useful for getinfo's clientversion output)

    FEATURE_WALLETCRYPT = 40000, // wallet encryption
    FEATURE_COMPRPUBKEY = 60000, // compressed public keys

    FEATURE_LATEST = 60000
};

enum AvailableCoinsType
{
    ALL_COINS = 1,
    ONLY_NOT10000IFMN = 3,
    ONLY_NONDENOMINATED_NOT10000IFMN = 4
};

/**
	Typedef
*/
typedef std::set<std::pair<const CWalletTx*, unsigned int>> setCoins_t;
typedef std::pair<const CWalletTx*, unsigned int> pairCoin_t;
typedef std::multimap<COutPoint, uint256> mmTxSpends_t;
typedef std::pair<mmTxSpends_t::const_iterator, mmTxSpends_t::const_iterator> mmTxSpendsRange_t;
typedef std::map<unsigned int, CMasterKey> mapMasterKeys_t;
typedef std::map<uint256, CWalletTx> mapWallet_t;
typedef std::map<CKeyID, CKeyMetadata> mapKeyMetadata_t;
typedef std::set<CStealthAddress> setStealthAddresses_t;
typedef std::map<uint256, int> mapRequestCount_t;
typedef std::map<CTxDestination, std::string> mapAddressBook_t;
typedef std::pair<CTxDestination, std::string> pairAddressBook_t;

/** A CWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
class CWallet : public CCryptoKeyStore, public CWalletInterface
{
private:
	/**
		Variables
	*/
    CWalletDB* pwalletdbEncryption;

    // the current wallet version: clients below this version are not able to load the wallet
    int nWalletVersion;

    // the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
    int nWalletMaxVersion;

    // Used to keep track of spent outpoints, and
    // detect and report conflicts (double-spends or
    // mutated transactions where the mutant gets mined).
    mmTxSpends_t mmTxSpends;
	
	/**
		Functions
	*/
	bool SelectCoinsForStaking(int64_t nTargetValue, unsigned int nSpendTime, setCoins_t& setCoinsRet, int64_t& nValueRet) const;
    
	//bool SelectCoins(int64_t nTargetValue, unsigned int nSpendTime, setCoins_t& setCoinsRet, int64_t& nValueRet,
	//		const CCoinControl *coinControl=NULL) const;
	
	bool SelectCoins(CAmount nTargetValue, unsigned int nSpendTime, setCoins_t& setCoinsRet, int64_t& nValueRet,
			const CCoinControl *coinControl = NULL, AvailableCoinsType coin_type=ALL_COINS, bool useIX = false) const;
    
	void AddToSpends(const COutPoint& outpoint, const uint256& wtxid);
    void AddToSpends(const uint256& wtxid);
    void SyncMetaData(std::pair<mmTxSpends_t::iterator, mmTxSpends_t::iterator>);

public:
	/**
		Variables
	*/
    /// Main wallet lock.
    /// This lock protects all the fields added by CWallet
    ///   except for:
    ///      fFileBacked (immutable after instantiation)
    ///      strWalletFile (immutable after instantiation)
    mutable CCriticalSection cs_wallet;
	
    bool fFileBacked;
    bool fWalletUnlockAnonymizeOnly;
    std::string strWalletFile;
    std::set<int64_t> setKeyPool;
    mapKeyMetadata_t mapKeyMetadata;
    setStealthAddresses_t stealthAddresses;
    StealthKeyMetaMap mapStealthKeyMeta;
    int nLastFilteredHeight;
    uint32_t nStealth, nFoundStealth; // for reporting, zero before use
    mapMasterKeys_t mapMasterKeys;
    unsigned int nMasterKeyMaxID;
	mapWallet_t mapWallet;
    std::list<CAccountingEntry> laccentries;
    TxItems wtxOrdered;
    int64_t nOrderPosNext;
    mapRequestCount_t mapRequestCount;
    mapAddressBook_t mapAddressBook;
    CPubKey vchDefaultKey;
    std::set<COutPoint> setLockedCoins;
    int64_t nTimeFirstKey;
	
	/**
		Signals
	*/
	/** Address book entry changed.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const CTxDestination &address, const std::string &label,
			bool isMine, ChangeType status)> NotifyAddressBookChanged;
	
    /** Wallet transaction added, removed or updated.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const uint256 &hashTx, ChangeType status)> NotifyTransactionChanged;

    /** Show progress e.g. for rescan */
    boost::signals2::signal<void (const std::string &title, int nProgress)> ShowProgress;

    /** Watch-only address added */
    boost::signals2::signal<void (bool fHaveWatchOnly)> NotifyWatchonlyChanged;
	
	/**
		Functions
	*/
    CWallet();
    CWallet(std::string strWalletFileIn);
	
	bool HasCollateralInputs(bool fOnlyConfirmed = true) const;
    bool IsCollateralAmount(int64_t nInputAmount) const;
    int  CountInputsWithAmount(int64_t nInputAmount);
    bool SelectCoinsCollateral(std::vector<CTxIn>& setCoinsRet, int64_t& nValueRet) const ;
    bool GetTransaction(const uint256 &hashTx, CWalletTx& wtx);
    bool GetStakeWeightFromValue(const int64_t& nTime, const int64_t& nValue, uint64_t& nWeight);
    void SetNull();
    const CWalletTx* GetWalletTx(const uint256& hash) const;
    bool CanSupportFeature(enum WalletFeature wf);
    void AvailableCoinsForStaking(std::vector<COutput>& vCoins, unsigned int nSpendTime) const;
	void AvailableCoins(std::vector<COutput>& vCoins, bool fOnlyConfirmed=true, const CCoinControl *coinControl = NULL,
			AvailableCoinsType coin_type=ALL_COINS, bool useIX = false) const;
    
	void AvailableCoinsMN(std::vector<COutput>& vCoins, bool fOnlyConfirmed=true, const CCoinControl *coinControl = NULL,
			AvailableCoinsType coin_type=ALL_COINS, bool useIX = false) const;
	
    bool SelectCoinsMinConf(int64_t nTargetValue, unsigned int nSpendTime, int nConfMine, int nConfTheirs,
			std::vector<COutput> vCoins, setCoins_t& setCoinsRet,
			int64_t& nValueRet) const;

    bool IsSpent(const uint256& hash, unsigned int n) const;
    bool IsLockedCoin(uint256 hash, unsigned int n) const;
    void LockCoin(COutPoint& output);
    void UnlockCoin(COutPoint& output);
    void UnlockAllCoins();
    void ListLockedCoins(std::vector<COutPoint>& vOutpts);
    CAmount GetTotalValue(std::vector<CTxIn> vCoins);
	
    // keystore implementation
    // Generate a new key
    CPubKey GenerateNewKey();
    // Adds a key to the store, and saves it to disk.
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey);
    // Adds a key to the store, without saving it to disk (used by LoadWallet)
    bool LoadKey(const CKey& key, const CPubKey &pubkey);
    // Load metadata (used by LoadWallet)
    bool LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &metadata);
    bool LoadMinVersion(int nVersion);
    // Adds an encrypted key to the store, and saves it to disk.
    bool AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);
    // Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);
    bool AddCScript(const CScript& redeemScript);
    bool LoadCScript(const CScript& redeemScript);

    // Adds a watch-only address to the store, and saves it to disk.
    bool AddWatchOnly(const CScript &dest);
    bool RemoveWatchOnly(const CScript &dest);
    // Adds a watch-only address to the store, without saving it to disk (used by LoadWallet)
    bool LoadWatchOnly(const CScript &dest);

    bool Lock();
    bool Unlock(const SecureString& strWalletPassphrase, bool anonymizeOnly = false, bool stakingOnly = false);
    bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase);
    bool EncryptWallet(const SecureString& strWalletPassphrase);
    void GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const;
	
    /** Increment the next transaction order id
        @return next transaction order id
     */
    int64_t IncOrderPosNext(CWalletDB *pwalletdb = NULL);

    void MarkDirty();
    bool AddToWallet(const CWalletTx& wtxIn, bool fFromLoadWallet=false);
    void SyncTransaction(const CTransaction& tx, const CBlock* pblock, bool fConnect = true, bool fFixSpentCoins = false);
    bool AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate);
    void EraseFromWallet(const uint256 &hash);
    int ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate = false);
    void ReacceptWalletTransactions();
    void ResendWalletTransactions(bool fForce = false);
    bool ImportPrivateKey(CDigitalNoteSecret vchSecret, std::string strLabel = "", bool fRescan = true);

    CAmount GetBalance() const;
    CAmount GetStake() const;
    CAmount GetNewMint() const;
    CAmount GetUnconfirmedBalance() const;
    CAmount GetImmatureBalance() const;
    CAmount GetWatchOnlyBalance() const;
    CAmount GetWatchOnlyStake() const;
    CAmount GetUnconfirmedWatchOnlyBalance() const;
    CAmount GetImmatureWatchOnlyBalance() const;

    bool CreateTransaction(const std::vector<std::pair<CScript, int64_t> >& vecSend, CWalletTx& wtxNew,
			CReserveKey& reservekey, int64_t& nFeeRet, int32_t& nChangePos, std::string& strFailReason,
			const CCoinControl *coinControl=NULL, AvailableCoinsType coin_type=ALL_COINS, bool useIX=false);
    
	bool CreateTransaction(CScript scriptPubKey, int64_t nValue, std::string& sNarr, CWalletTx& wtxNew,
			CReserveKey& reservekey, int64_t& nFeeRet, const CCoinControl *coinControl=NULL);
    
	bool CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey, std::string strCommand="tx");

    bool AddAccountingEntry(const CAccountingEntry&, CWalletDB & pwalletdb);

    uint64_t GetStakeWeight() const;
    bool CreateCoinStake(const CKeyStore& keystore, unsigned int nBits, int64_t nSearchInterval, int64_t nFees,
			CTransaction& txNew, CKey& key);

    std::string SendMoney(CScript scriptPubKey, int64_t nValue, std::string& sNarr, CWalletTx& wtxNew, bool fAskFee=false);
    std::string SendMoneyToDestination(const CTxDestination &address, int64_t nValue, std::string& sNarr, CWalletTx& wtxNew,
			bool fAskFee=false);

    bool NewStealthAddress(std::string& sError, std::string& sLabel, CStealthAddress& sxAddr);
    bool AddStealthAddress(CStealthAddress& sxAddr);
    bool UnlockStealthAddresses(const CKeyingMaterial& vMasterKeyIn);
    bool UpdateStealthAddress(std::string &addr, std::string &label, bool addIfNotExist);

    bool CreateStealthTransaction(CScript scriptPubKey, int64_t nValue, std::vector<uint8_t>& P, std::vector<uint8_t>& narr,
			std::string& sNarr, CWalletTx& wtxNew, CReserveKey& reservekey, int64_t& nFeeRet,
			const CCoinControl* coinControl=NULL);

	std::string SendStealthMoney(CScript scriptPubKey, int64_t nValue, std::vector<uint8_t>& P, std::vector<uint8_t>& narr,
			std::string& sNarr, CWalletTx& wtxNew, bool fAskFee=false);
	
    bool SendStealthMoneyToDestination(CStealthAddress& sxAddress, int64_t nValue, std::string& sNarr, CWalletTx& wtxNew,
		std::string& sError, bool fAskFee=false);
	
    bool FindStealthTransactions(const CTransaction& tx, mapValue_t& mapNarr);

    bool CreateCollateralTransaction(CTransaction& txCollateral, std::string& strReason);
    bool ConvertList(std::vector<CTxIn> vCoins, std::vector<int64_t>& vecAmounts);

    bool NewKeyPool();
    bool TopUpKeyPool(unsigned int nSize = 0);
    int64_t AddReserveKey(const CKeyPool& keypool);
    void ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool);
    void KeepKey(int64_t nIndex);
    void ReturnKey(int64_t nIndex);
    bool GetKeyFromPool(CPubKey &key);
    int64_t GetOldestKeyPoolTime();
    void GetAllReserveKeys(std::set<CKeyID>& setAddress) const;

    std::set<std::set<CTxDestination>> GetAddressGroupings();
    std::map<CTxDestination, int64_t> GetAddressBalances();
	
    isminetype IsMine(const CTxIn& txin) const;
    CAmount GetDebit(const CTxIn& txin, const isminefilter& filter) const;
    isminetype IsMine(const CTxOut& txout) const;
    CAmount GetCredit(const CTxOut& txout, const isminefilter& filter) const;
    bool IsChange(const CTxOut& txout) const;
    CAmount GetChange(const CTxOut& txout) const;
    bool IsMine(const CTransaction& tx) const;
    /** should probably be renamed to IsRelevantToMe */
    bool IsFromMe(const CTransaction& tx) const;
    CAmount GetDebit(const CTransaction& tx, const isminefilter& filter) const;
    CAmount GetCredit(const CTransaction& tx, const isminefilter& filter) const;
    CAmount GetChange(const CTransaction& tx) const;
    void SetBestChain(const CBlockLocator& loc);

    DBErrors LoadWallet(bool& fFirstRunRet);

    bool SetAddressBookName(const CTxDestination& address, const std::string& strName);

    bool SetAddressAccountIdAssociation(const CTxDestination& address, const std::string& strName);

    bool DelAddressBookName(const CTxDestination& address);

    bool UpdatedTransaction(const uint256 &hashTx);

    void Inventory(const uint256 &hash);
    unsigned int GetKeyPoolSize();
    bool SetDefaultKey(const CPubKey &vchPubKey);

    /**
		signify that a particular wallet feature is now used. this may change nWalletVersion and nWalletMaxVersion
		if those are lower
	*/
    bool SetMinVersion(enum WalletFeature, CWalletDB* pwalletdbIn = NULL, bool fExplicit = false);

    /**
		change which version we're allowed to upgrade to (note that this does not immediately imply upgrading
		to that format)
	*/
    bool SetMaxVersion(int nVersion);

    // get the current wallet format (the oldest client version guaranteed to understand this wallet)
    int GetVersion();

    // Get wallet transactions that conflict with given transaction (spend same outputs)
    std::set<uint256> GetConflicts(const uint256& txid) const;

    void FixSpentCoins(int& nMismatchSpent, int64_t& nBalanceInQuestion, bool fCheckOnly = false);
    void DisableTransaction(const CTransaction &tx);
	
	/**
		? Not found ?
	*/
	bool SelectCoinsDark(int64_t nValueMin, int64_t nValueMax, std::vector<CTxIn>& setCoinsRet, int64_t& nValueRet,
			int nMNengineRoundsMin, int nMNengineRoundsMax) const;
	
	bool SelectCoinsMasternode(CTxIn& vin, int64_t& nValueRet, CScript& pubScript) const;
	
	CAmount GetAnonymizableBalance() const;
    CAmount GetAnonymizedBalance() const;
	double GetAverageAnonymizedRounds() const;
    CAmount GetNormalizedAnonymizedBalance() const;
	
	int GenerateMNengineOutputs(int nTotalValue, std::vector<CTxOut>& vout);
	
	// get the MNengine chain depth for a given input
    int GetRealInputMNengineRounds(CTxIn in, int rounds) const;
    // respect current settings
    int GetInputMNengineRounds(CTxIn in) const;
};

void ApproximateBestSubset(std::vector<std::pair<int64_t, std::pair<const CWalletTx*,unsigned int> > >vValue,
		int64_t nTotalLower, int64_t nTargetValue, std::vector<char>& vfBest, int64_t& nBest, int iterations = 1000);

extern int64_t GetStakeCombineThreshold();
int64_t GetStakeSplitThreshold();

#endif // CWALLET_H
