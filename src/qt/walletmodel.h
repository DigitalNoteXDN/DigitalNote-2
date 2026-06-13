#include <QString>
#ifndef WALLETMODEL_H
#define WALLETMODEL_H

#include "compat.h"

#include <map>
#include <vector>
#include <functional>
#include <string>

#include <QObject>

#include "allocators.h" /* for SecureString */
#include "instantx.h"
#include "cwallet.h"
#include "cscript.h"
#include "coutpoint.h"
#include <bip39/bip39_wallet.h>
#include "serialize.h"
#include "walletmodeltransaction.h"

class AddressTableModel;
class OptionsModel;
class TransactionTableModel;
class WalletModelTransaction;

class CWallet;
class CKeyID;
class CPubKey;
class COutput;
class COutPoint;
class uint256;
class CCoinControl;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class SendCoinsRecipient
{
public:
    explicit SendCoinsRecipient() : amount(0), nVersion(SendCoinsRecipient::CURRENT_VERSION)
	{
		
	}
    explicit SendCoinsRecipient(const QString &addr, const QString &label, const CAmount& amount, const QString &message)
			: address(addr), label(label), amount(amount), message(message), nVersion(SendCoinsRecipient::CURRENT_VERSION)
	{
		
	}

    // If from an insecure payment request, this is used for storing
    // the addresses, e.g. address-A<br />address-B<br />address-C.
    // Info: As we don't need to process addresses in here when using
    // payment requests, we can abuse it for displaying an address list.
    // Todo: This is a hack, should be replaced with a cleaner solution!
    QString address;
    QString label;
    AvailableCoinsType inputType;
    bool useInstantX;
    CAmount amount;
    // If from a payment request, this is used for storing the memo
    QString message;

    int typeInd;

    // Empty if no authentication or invalid signature/cert/etc.
    QString authenticatedMerchant;

    static const int CURRENT_VERSION = 1;
    int nVersion;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        std::string sAddress = address.toStdString();
        std::string sLabel = label.toStdString();
        std::string sMessage = message.toStdString();

        unsigned int nSerSize = 0;
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(sAddress);
        READWRITE(sLabel);
        READWRITE(amount);
        READWRITE(sMessage);

        if (ser_action.ForRead())
        {
            address = QString::fromStdString(sAddress);
            label = QString::fromStdString(sLabel);
            message = QString::fromStdString(sMessage);
        }
    }
};

/** Per-row detail for the Locked Outputs dialog (Tools menu).
 *  Built by WalletModel::listLockedOutputsWithDetails which enriches
 *  each entry from setLockedCoins with address, amount, address-book
 *  label, and a classification of what the output is.
 *
 *  Tier classification (see spec):
 *    - LOT_MASTERNODE: txid:vout appears in masternode.conf.  label
 *      is the MN alias from the conf file.
 *    - LOT_MN_COLLATERAL_AMOUNT: amount equals the MN collateral
 *      amount but NOT in masternode.conf.  Likely an abandoned MN
 *      collateral or popup-locked-but-unconfigured.  Warn before
 *      unlock but don't claim it's a configured MN.
 *    - LOT_OTHER: regular user lock (e.g. cold storage, custom
 *      reservations).  Standard unlock confirmation only.
 *
 *  Watch-only outputs are filtered at population time.  This struct
 *  represents only outputs the wallet can actually spend.
 */
struct LockedOutputDetail
{
    enum Tier
    {
        LOT_MASTERNODE,
        LOT_MN_COLLATERAL_AMOUNT,
        LOT_OTHER
    };

    COutPoint outpoint;       // txid:vout
    QString address;          // base58 receiving address (may be empty
                              // if the tx is reorged out / not in mapWallet)
    QString addressLabel;     // address-book label, may be empty
    qint64 amount;            // value in satoshis (XDN_COIN units)
    Tier tier;
    QString masternodeAlias;  // populated only when tier == LOT_MASTERNODE
};

/** Interface to DigitalNote wallet from Qt view code. */
class WalletModel : public QObject
{
    Q_OBJECT

public:
    explicit WalletModel(CWallet *wallet, OptionsModel *optionsModel, QObject *parent = 0);
    ~WalletModel();

    enum StatusCode // Returned by sendCoins
    {
        OK,
        InvalidAmount,
        InvalidAddress,
        AmountExceedsBalance,
        AmountWithFeeExceedsBalance,
        DuplicateAddress,
        TransactionCreationFailed, // Error returned when wallet is still locked
        IXTransactionCreationFailed, // Error returned when InstantX fails in prepareTransaction
        PrepareTransactionFailed, // Error returned when InstantX fails in prepareTransaction
        TransactionCommitFailed,
        Aborted,
        InsaneFee
    };

    enum EncryptionStatus
    {
        Unencrypted,  // !wallet->IsCrypted()
        Locked,       // wallet->IsCrypted() && wallet->IsLocked()
        Unlocked,      // wallet->IsCrypted() && !wallet->IsLocked()
    };

    OptionsModel *getOptionsModel();
    AddressTableModel *getAddressTableModel();
    TransactionTableModel *getTransactionTableModel();

    CAmount getBalance(const CCoinControl *coinControl=NULL) const;
    CAmount getStake() const;
    CAmount getUnconfirmedBalance() const;
    CAmount getImmatureBalance() const;
    bool haveWatchOnly() const;
    CAmount getWatchBalance() const;
    CAmount getWatchStake() const;
    CAmount getWatchUnconfirmedBalance() const;
    CAmount getWatchImmatureBalance() const;

    /** Single watch-only entry, populated by getWatchOnlyEntries().
     *  - displayAddress: extracted destination as XDN address (or empty
     *    if the script doesn't decode to a single address, e.g. a
     *    multisig redeem script imported via importaddress hex)
     *  - label: from the wallet address book (empty if no label set)
     *  - script: the underlying CScript record key, needed to call
     *    RemoveWatchOnly */
    struct WatchOnlyEntry
    {
        QString  displayAddress;
        QString  label;
        CScript  script;
    };

    /** Enumerate all currently-watched scripts.  Returns each as a
     *  WatchOnlyEntry with display-friendly fields.  Safe to call
     *  while wallet is locked; takes cs_wallet internally. */
    std::vector<WatchOnlyEntry> getWatchOnlyEntries() const;

    /** Remove a single watch-only entry.  Returns true on success.
     *  Used by WatchOnlyWorker for off-thread bulk removal. */
    bool removeWatchOnly(const CScript &script);

    /** Per-script progress callback signature for the overload below.
     *  percent is 0-100 within the single script's removal.
     *  label is a human-readable phase hint. */
    typedef std::function<void(int percent, const std::string& label)> RemoveWatchOnlyProgressFn;

    /** Same as removeWatchOnly but reports per-script progress via the
     *  callback.  Used by WatchOnlyWorker so the GUI progress bar can
     *  move smoothly while a single big address is being removed. */
    bool removeWatchOnly(const CScript &script, const RemoveWatchOnlyProgressFn& progressCb);

    /** Force a fresh read of watch-only state from the wallet,
     *  bypassing the cached fHaveWatchOnly flag and the queued
     *  NotifyWatchonlyChanged signal path.  Sets fForceCheckBalanceChanged
     *  so the next poll tick recomputes balances, and emits
     *  notifyWatchonlyChanged synchronously to update GUI visibility.
     *  Use after batch operations (e.g. RemoveWatchOnlyDialog) to
     *  guarantee the GUI reflects current wallet state without waiting
     *  for the event loop to drain. */
    void refreshWatchOnlyState();

    EncryptionStatus getEncryptionStatus() const;

    // Check address for validity
    bool validateAddress(const QString &address);

    // Return status record for SendCoins, contains error id + information
    struct SendCoinsReturn
    {
        SendCoinsReturn(StatusCode status = OK):
            status(status) {}
        StatusCode status;
    };

    // prepare transaction for getting txfee before sending coins
    SendCoinsReturn prepareTransaction(WalletModelTransaction &transaction, const CCoinControl *coinControl = NULL);

    // Send coins to a list of recipients
    SendCoinsReturn sendCoins(WalletModelTransaction &transaction, const CCoinControl *coinControl);

    // Wallet encryption
    bool setWalletEncrypted(bool encrypted, const SecureString &passphrase);
    // Passphrase only needed when unlocking
    bool setWalletLocked(bool locked, const SecureString &passPhrase=SecureString(), bool stakingOnly=false);
    bool changePassphrase(const SecureString &oldPass, const SecureString &newPass);
    // Wallet backup
    bool backupWallet(const QString &filename);

    // BIP39 recovery phrase — derives a 24-word mnemonic from the wallet passphrase.
    // Call after encryption to show the user their recovery phrase.
    // passphrase is the raw encryption passphrase just typed by the user.
    bool generateMnemonic(BIP39Wallet::WordCount wordCount, SecureString &mnemonic) const;
    bool hasRecoveryPhraseSupport() const;
    bool hasMnemonicMasterKey() const;

    /** True iff the wallet should be offered the recovery-phrase upgrade
     *  prompt.  Combines: encrypted, no CMasterKey[2], not previously
     *  declined for this wallet.  The "declined" half is a UI preference
     *  per-wallet in QSettings (see src/qt/guistate.h). */
    bool needsRecoveryPhraseUpgrade() const;

    /** Persistently record that the user has declined the upgrade for
     *  THIS wallet.  Per-wallet: a different wallet file gets prompted
     *  afresh.  Stored in QSettings via GuiState. */
    void setRecoveryPhraseUpgradeDeclined();

    /** Generate the mnemonic master key from the unlocked wallet's
     *  vMasterKey.  Wallet must be unlocked.  Returns false if locked. */
    bool addMnemonicMasterKey();

    /** Remove the mnemonic master key entry (rarely needed in D2). */
    bool removeMnemonicMasterKey();

    /** Used by the unlock-with-mnemonic path to confirm a typed
     *  passphrase matches the wallet's master key. */
    bool verifyPassphrase(const SecureString &passphrase) const;

    /** Re-derive the current mnemonic from vMasterKey.  Wallet must be
     *  unlocked.  Output is a SecureString of space-separated words.
     *  Returns false on failure (locked, not encrypted, internal error). */
    bool getCurrentMnemonic(SecureString &mnemonicOut) const;

    /** Rotate the wallet's master key.  Wallet must be unlocked.
     *  Returns true on success and populates newMnemonicOut with the new
     *  recovery phrase.  See CWallet::RotateMnemonicMasterKey() for the
     *  full contract. */
    bool rotateRecoveryPhrase(const SecureString &currentPassword,
                              SecureString &newMnemonicOut);


    // Re-enable unlock via mnemonic: derives passphrase from mnemonic and unlocks.
	// Wallet Repair
	void checkWallet(int& nMismatchSpent, int64_t& nBalanceInQuestion);
	void repairWallet(int& nMismatchSpent, int64_t& nBalanceInQuestio);
	
    // RAI object for unlocking wallet, returned by requestUnlock()
    class UnlockContext
    {
    public:
        UnlockContext(WalletModel *wallet, bool valid, bool relock);
        ~UnlockContext();

        bool isValid() const { return valid; }

        // Copy operator and constructor transfer the context
        UnlockContext(const UnlockContext& obj) { CopyFrom(obj); }
        UnlockContext& operator=(const UnlockContext& rhs) { CopyFrom(rhs); return *this; }
    private:
        WalletModel *wallet;
        bool valid;
        mutable bool relock; // mutable, as it can be set to false by copying

        void CopyFrom(const UnlockContext& rhs);
    };

    UnlockContext requestUnlock();
    UnlockContext requestUnlockWithMnemonic(const QString &mnemonic);

    bool getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const;
    void getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs);
    bool isSpent(const COutPoint& outpoint) const;
    void listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const;
    bool isLockedCoin(uint256 hash, unsigned int n) const;
    void lockCoin(COutPoint& output);
    void unlockCoin(COutPoint& output);
    void listLockedCoins(std::vector<COutPoint>& vOutpts);

    /** Locked Outputs dialog support: walk setLockedCoins, filter
     *  watch-only outputs out, classify the rest into the three
     *  LockedOutputDetail tiers, and return the enriched list. */
    void listLockedOutputsWithDetails(std::vector<LockedOutputDetail>& vDetails);

    /** Helper to emit collateralCandidateReceived from non-QObject
     *  callers (TransactionTablePriv).  Direct emit isn't possible from
     *  outside a QObject subclass; this method is the trampoline. */
    void emitCollateralCandidate(const QString &txidHex, int vout);
    bool processingQueuedTransactions() { return fProcessingQueuedTransactions; }

private:
    CWallet *wallet;
    bool fHaveWatchOnly;
    bool fForceCheckBalanceChanged;
    bool fProcessingQueuedTransactions;

    // Wallet has an options model for wallet-specific options
    // (transaction fee, for example)
    OptionsModel *optionsModel;

    AddressTableModel *addressTableModel;
    TransactionTableModel *transactionTableModel;

    // Cache some values to be able to detect changes
    CAmount cachedBalance;
    CAmount cachedStake;
    CAmount cachedUnconfirmedBalance;
    CAmount cachedImmatureBalance;
    CAmount cachedWatchOnlyBalance;
    CAmount cachedWatchOnlyStake;
    CAmount cachedWatchUnconfBalance;
    CAmount cachedWatchImmatureBalance;
    CAmount cachedNumTransactions;
    EncryptionStatus cachedEncryptionStatus;
    int cachedNumBlocks;
    int cachedTxLocks;
    int cachedMNengineRounds;

    QTimer *pollTimer;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();
    void checkBalanceChanged();

signals:
    // Signal that balance in wallet changed
    void balanceChanged(const CAmount& balance, const CAmount& stake, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
        const CAmount& watchOnlyBalance, const CAmount& watchOnlyStake, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);

    // Encryption status of wallet changed
    void encryptionStatusChanged(int status);
     /** Emitted when the wallet transitions from Locked to Unlocked AND
     *  the wallet still needs the recovery-phrase upgrade.  Connected
     *  in BitcoinGUI to display the upgrade dialog. */
    void recoveryPhraseUpgradeAvailable();

    // Signal emitted when wallet needs to be unlocked
    // It is valid behaviour for listeners to keep the wallet locked after this signal;
    // this means that the unlocking failed or was cancelled.
    void requireUnlock();

    // Asynchronous message notification
    void message(const QString &title, const QString &message, bool modal, unsigned int style);

    // Coins sent: from wallet, to recipient, in (serialized) transaction:
    void coinsSent(CWallet* wallet, SendCoinsRecipient recipient, QByteArray transaction);

    // Show progress dialog e.g. for rescan
    void showProgress(const QString &title, int nProgress);

    // Watch-only address added
    void notifyWatchonlyChanged(bool fHaveWatchonly);

    /** Emitted when an incoming transaction creates a UTXO that looks
     *  like fresh masternode collateral: exactly 2,000,000 XDN, owned
     *  by us, spendable, not already locked, and the user has not
     *  globally suppressed the prompt for this wallet (see GuiState
     *  is2MCollateralPromptSuppressed).  BitcoinGUI handles the prompt
     *  dialog.  Fired from CT_NEW handling in TransactionTablePriv. */
    void collateralCandidateReceived(const QString &txidHex, int vout);

public slots:
    /* Wallet status might have changed */
    void updateStatus();
    /* New transaction, or transaction changed status */
    void updateTransaction();
    /* New, updated or removed address book entry */
    void updateAddressBook(const QString &address, const QString &label, bool isMine, int status);
    /* Watch-only added */
    void updateWatchOnlyFlag(bool fHaveWatchonly);
    /* Current, immature or unconfirmed balance might have changed - emit 'balanceChanged' if so */
    void pollBalanceChanged();
    /* Needed to update fProcessingQueuedTransactions through a QueuedConnection */
    void setProcessingQueuedTransactions(bool value) { fProcessingQueuedTransactions = value; }

};

#endif // WALLETMODEL_H