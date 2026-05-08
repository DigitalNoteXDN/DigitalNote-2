#include "compat.h"

#include <stdint.h>
#include <QDebug>
#include <QSet>
#include <QTimer>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>

#include "init.h"
#include "addresstablemodel.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "transactiontablemodel.h"
#include "checkpoints.h"
#include "db.h"
#include "walletdb.h" // for BackupWallet
#include "spork.h"
#include "coutput.h"
#include "cwallettx.h"
#include "creservekey.h"
#include "wallet.h"
#include "script.h"
#include "main_extern.h"
#include "main_const.h"
#include "thread.h"
#include "ctxin.h"
#include "ctxout.h"
#include "coutpoint.h"
#include "coutput.h"
#include "cdigitalnoteaddress.h"
#include "cblockindex.h"
#include "masternodeconfig.h"
#include "cmasternodeconfig.h"
#include "cmasternodeconfigentry.h"
#include "mining.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "ui_interface.h"
#include "util.h"   // for LogPrintf

#include "walletmodel.h"
#include "guistate.h"
#include <bip39/bip39_wallet.h>
#include <bip39/bip39_passphrase.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>

WalletModel::WalletModel(CWallet *wallet, OptionsModel *optionsModel, QObject *parent) :
    QObject(parent), wallet(wallet),
    fProcessingQueuedTransactions(false),
    optionsModel(optionsModel), addressTableModel(0), transactionTableModel(0),
    cachedBalance(0), cachedStake(0), cachedUnconfirmedBalance(0), cachedImmatureBalance(0),
    cachedEncryptionStatus(Unencrypted),
    cachedNumBlocks(0)
{
    fHaveWatchOnly = wallet->HaveWatchOnly();
    fForceCheckBalanceChanged = false;

    addressTableModel = new AddressTableModel(wallet, this);
    transactionTableModel = new TransactionTableModel(wallet, this);

    // This timer will be fired repeatedly to update the balance
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(pollBalanceChanged()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

WalletModel::~WalletModel()
{
    unsubscribeFromCoreSignals();
}

CAmount WalletModel::getBalance(const CCoinControl *coinControl) const
{
	if (coinControl)
	{
		CAmount nBalance = 0;
		std::vector<COutput> vCoins;
		
		wallet->AvailableCoins(vCoins, true, coinControl);
		
		for(const COutput& out : vCoins)
		{
			if(out.fSpendable)
			{
				nBalance += out.tx->vout[out.i].nValue;
			}
		}
		
		return nBalance;
	}

	return wallet->GetBalance();
}

CAmount WalletModel::getStake() const
{
    return wallet->GetStake();
}

CAmount WalletModel::getUnconfirmedBalance() const
{
    return wallet->GetUnconfirmedBalance();
}

CAmount WalletModel::getImmatureBalance() const
{
    return wallet->GetImmatureBalance();
}

bool WalletModel::haveWatchOnly() const
{
    return fHaveWatchOnly;
}

CAmount WalletModel::getWatchBalance() const
{
    return wallet->GetWatchOnlyBalance();
}

CAmount WalletModel::getWatchStake() const
{
    return wallet->GetWatchOnlyStake();
}

CAmount WalletModel::getWatchUnconfirmedBalance() const
{
    return wallet->GetUnconfirmedWatchOnlyBalance();
}

CAmount WalletModel::getWatchImmatureBalance() const
{
    return wallet->GetImmatureWatchOnlyBalance();
}

std::vector<WalletModel::WatchOnlyEntry> WalletModel::getWatchOnlyEntries() const
{
    std::vector<WatchOnlyEntry> result;

    std::set<CScript> setScripts;
    {
        // GetWatchOnly takes cs_KeyStore internally, no need to wrap.
        wallet->GetWatchOnly(setScripts);
    }

    if (setScripts.empty())
    {
        return result;
    }

    // Look up labels under cs_wallet (mapAddressBook is wallet state).
    LOCK(wallet->cs_wallet);

    for (const CScript& script : setScripts)
    {
        WatchOnlyEntry entry;
        entry.script = script;

        CTxDestination dest;
        if (ExtractDestination(script, dest))
        {
            CDigitalNoteAddress addr(dest);
            entry.displayAddress = QString::fromStdString(addr.ToString());

            // Look up address book label
            auto it = wallet->mapAddressBook.find(dest);
            if (it != wallet->mapAddressBook.end())
            {
                entry.label = QString::fromStdString(it->second);
            }
        }
        else
        {
            // Script doesn't extract to a single destination (e.g. a raw
            // P2SH redeem script imported in hex form via importaddress).
            // Show "(script)" so the user can still distinguish entries
            // even without a friendly address.
            entry.displayAddress = tr("(script)");
        }

        result.push_back(entry);
    }

    return result;
}

bool WalletModel::removeWatchOnly(const CScript &script)
{
    LOCK2(cs_main, wallet->cs_wallet);

    if (!wallet->HaveWatchOnly(script))
    {
        // Already gone (race with another remover, or stale dialog state)
        return false;
    }

    if (!wallet->RemoveWatchOnly(script))
    {
        return false;
    }

    wallet->MarkDirty();
    return true;
}

bool WalletModel::removeWatchOnly(const CScript &script, const RemoveWatchOnlyProgressFn& progressCb)
{
    LOCK2(cs_main, wallet->cs_wallet);

    if (!wallet->HaveWatchOnly(script))
    {
        // Already gone (race with another remover, or stale dialog state)
        return false;
    }

    // Pass the callback through to CWallet::RemoveWatchOnly, which
    // reports per-phase progress (scan/erase/refresh) so the dialog
    // bar can move while a single watch-only address with thousands
    // of historical transactions is being removed.
    if (!wallet->RemoveWatchOnly(script, progressCb))
    {
        return false;
    }

    wallet->MarkDirty();
    return true;
}

void WalletModel::updateStatus()
{
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();
 
    LogPrintf("WalletModel::updateStatus: cached=%d new=%d\n",
              (int)cachedEncryptionStatus, (int)newEncryptionStatus);
        // Detect any transition INTO Unlocked state, regardless of where we
        // came from.  This handles three scenarios:
        //   - User just typed their password to unlock (Locked -> Unlocked)
        //   - Wallet was loaded already-unlocked at startup, e.g. just-encrypted
        //     (Unencrypted -> Unlocked, the first time we observe state)
        //   - Initial cache miss: cachedEncryptionStatus was the constructor
        //     default Unencrypted but the user unlocked between wallet load
        //     and the first updateStatus poll
        //
        // In all three, the moment the wallet becomes (or is observed as)
        // unlocked is the right time to consider the upgrade prompt.  The
        // needsRecoveryPhraseUpgrade() check inside the if-block ensures
        // we don't actually emit unless the wallet truly needs it.
        if(cachedEncryptionStatus != newEncryptionStatus) {
        bool justUnlocked = (newEncryptionStatus    == Unlocked
                      && cachedEncryptionStatus != Unlocked);
 
        LogPrintf("WalletModel::updateStatus: TRANSITION (justUnlocked=%d)\n",
                  (int)justUnlocked);
 
        emit encryptionStatusChanged(newEncryptionStatus);
        cachedEncryptionStatus = newEncryptionStatus;
 
        if (justUnlocked) {
            bool needs = needsRecoveryPhraseUpgrade();
            LogPrintf("WalletModel::updateStatus: justUnlocked, needsUpgrade=%d\n",
                      (int)needs);
            if (needs) {
                LogPrintf("WalletModel::updateStatus: emitting recoveryPhraseUpgradeAvailable\n");
                emit recoveryPhraseUpgradeAvailable();
            }
        }
    }
}

void WalletModel::pollBalanceChanged()
{
    // Defence-in-depth: skip polling until wallet load + ReacceptWalletTransactions
    // have completed. WalletModel itself is constructed after AppInit2 returns,
    // so this gate would not normally fire -- but if anything ever wires up an
    // earlier WalletModel construction, this guard prevents the same kind of
    // partial-keystore cache poisoning that the staking-icon poll caused in
    // v2.0.0.7 (see bitcoingui.cpp:updateWeight for the original symptom).
    if (!fWalletLoadComplete)
        return;

    // Get required locks upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(cs_main, lockMain);
    if(!lockMain)
        return;
    TRY_LOCK(wallet->cs_wallet, lockWallet);
    if(!lockWallet)
        return;

    if(fForceCheckBalanceChanged || nBestHeight != cachedNumBlocks || cachedTxLocks != nCompleteTXLocks)
    {
        fForceCheckBalanceChanged = false;

        // Balance and number of transactions might have changed
        cachedNumBlocks = nBestHeight;

        checkBalanceChanged();
        if(transactionTableModel)
            transactionTableModel->updateConfirmations();
    }
}

void WalletModel::checkBalanceChanged()
{
    TRY_LOCK(cs_main, lockMain);
    if(!lockMain) return;

    CAmount newBalance = getBalance();
    CAmount newStake = getStake();
    CAmount newUnconfirmedBalance = getUnconfirmedBalance();
    CAmount newImmatureBalance = getImmatureBalance();
    CAmount newWatchOnlyBalance = 0;
    CAmount newWatchOnlyStake = 0;
    CAmount newWatchUnconfBalance = 0;
    CAmount newWatchImmatureBalance = 0;
    if (haveWatchOnly())
    {
        newWatchOnlyBalance = getWatchBalance();
        newWatchOnlyStake = getWatchStake();
        newWatchUnconfBalance = getWatchUnconfirmedBalance();
        newWatchImmatureBalance = getWatchImmatureBalance();
    }

    if(cachedBalance != newBalance || cachedStake != newStake || cachedUnconfirmedBalance != newUnconfirmedBalance || cachedImmatureBalance != newImmatureBalance ||
        cachedTxLocks != nCompleteTXLocks || cachedWatchOnlyBalance != newWatchOnlyBalance || cachedWatchOnlyStake != newWatchOnlyStake || cachedWatchUnconfBalance != newWatchUnconfBalance || cachedWatchImmatureBalance != newWatchImmatureBalance)
    {
        cachedBalance = newBalance;
        cachedStake = newStake;
        cachedUnconfirmedBalance = newUnconfirmedBalance;
        cachedImmatureBalance = newImmatureBalance;
        cachedTxLocks = nCompleteTXLocks;
        cachedWatchOnlyBalance = newWatchOnlyBalance;
        cachedWatchUnconfBalance = newWatchUnconfBalance;
        cachedWatchImmatureBalance = newWatchImmatureBalance;
        emit balanceChanged(newBalance, newStake, newUnconfirmedBalance, newImmatureBalance,
            newWatchOnlyBalance, newWatchOnlyStake, newWatchUnconfBalance, newWatchImmatureBalance);
    }
}

void WalletModel::updateTransaction()
{
    // Balance and number of transactions might have changed
    fForceCheckBalanceChanged = true;
}

void WalletModel::updateAddressBook(const QString &address, const QString &label, bool isMine, int status)
{
    if(addressTableModel)
        addressTableModel->updateEntry(address, label, isMine, status);
}

void WalletModel::updateWatchOnlyFlag(bool fHaveWatchonly)
{
    fHaveWatchOnly = fHaveWatchonly;
    // Watch-only state change implies balance figures need to be
    // recomputed (newly-imported addresses may have credits, removed
    // addresses leave behind stale cached values).  Without forcing a
    // recheck, balance polling won't notice until the next block tick.
    fForceCheckBalanceChanged = true;
    emit notifyWatchonlyChanged(fHaveWatchonly);
}

void WalletModel::refreshWatchOnlyState()
{
    // Read fresh state from the wallet itself, bypassing the cached
    // fHaveWatchOnly flag.  Take cs_wallet to ensure we read consistent
    // state -- HaveWatchOnly() iterates setWatchOnly which can be
    // mutated by other threads.
    bool freshState;
    {
        LOCK(wallet->cs_wallet);
        freshState = wallet->HaveWatchOnly();
    }

    fHaveWatchOnly = freshState;
    fForceCheckBalanceChanged = true;

    // Synchronous emission -- subscribers on the same thread (overview,
    // transaction view) update immediately, no event loop wait.
    emit notifyWatchonlyChanged(freshState);
}

bool WalletModel::validateAddress(const QString &address)
{
    std::string sAddr = address.toStdString();

    if (sAddr.length() > 75)
    {
        if (IsStealthAddress(sAddr))
            return true;
    };

    CDigitalNoteAddress addressParsed(sAddr);
    return addressParsed.IsValid();
}

WalletModel::SendCoinsReturn WalletModel::prepareTransaction(WalletModelTransaction &transaction, const CCoinControl *coinControl)
{
    CAmount total = 0;
    QList<SendCoinsRecipient> recipients = transaction.getRecipients();
    std::vector<std::pair<CScript, CAmount> > vecSend;

    if(recipients.empty())
    {
        return OK;
    }

    QSet<QString> setAddress; // Used to detect duplicates
    int nAddresses = 0;

    // Pre-check input data for validity
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        if(!validateAddress(rcp.address))
        {
            return InvalidAddress;
        }
        if(rcp.amount <= 0)
        {
            return InvalidAmount;
        }
        setAddress.insert(rcp.address);
        ++nAddresses;

        CScript scriptPubKey = GetScriptForDestination(CDigitalNoteAddress(rcp.address.toStdString()).Get());
        vecSend.push_back(std::pair<CScript, CAmount>(scriptPubKey, rcp.amount));

        total += rcp.amount;
    }
    if(setAddress.size() != nAddresses)
    {
        return DuplicateAddress;
    }

    CAmount nBalance = getBalance(coinControl);

    if(total > nBalance)
    {
        return AmountExceedsBalance;
    }

    {
        LOCK2(cs_main, wallet->cs_wallet);

        transaction.newPossibleKeyChange(wallet);
        CAmount nFeeRequired = 0;
        std::string strFailReason;

        CWalletTx *newTx = transaction.getTransaction();
        CReserveKey *keyChange = transaction.getPossibleKeyChange();

        if(recipients[0].useInstantX && total > GetSporkValue(SPORK_5_MAX_VALUE)*COIN){
            return IXTransactionCreationFailed;
        }

        int nChangePos;
        bool fCreated = wallet->CreateTransaction(vecSend, *newTx, *keyChange, nFeeRequired, nChangePos, strFailReason, coinControl, recipients[0].inputType, recipients[0].useInstantX);
        transaction.setTransactionFee(nFeeRequired);

        if(recipients[0].useInstantX && newTx->GetValueOut() > GetSporkValue(SPORK_5_MAX_VALUE)*COIN){
            return IXTransactionCreationFailed;
        }

        if(!fCreated)
        {
            if((total + nFeeRequired) > nBalance)
            {
                return SendCoinsReturn(AmountWithFeeExceedsBalance);
            }
            emit message(tr("Send Coins"), QString::fromStdString(strFailReason), false,
                         CClientUIInterface::MSG_ERROR);
            return PrepareTransactionFailed;
        }
        // reject insane fee
        unsigned int insanefee = (transaction.getTransactionSize() == 0 ? nAddresses : transaction.getTransactionSize());
        insanefee = ((MIN_RELAY_TX_FEE * insanefee) * 10000);
        if (nFeeRequired > insanefee){
            LogPrintf("nFeeRequired: %d -- InsaneFee: %d\n", nFeeRequired, insanefee);
            return InsaneFee;
        }
    }

    return SendCoinsReturn(OK);
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(WalletModelTransaction &transaction, const CCoinControl *coinControl)
{
    QByteArray transaction_array; /* store serialized transaction */
    CAmount total = 0;
    QSet<QString> setAddress;
    QList<SendCoinsRecipient> recipients = transaction.getRecipients();
    std::vector<std::pair<CScript, CAmount> > vecSend;

    {
        LOCK2(cs_main, wallet->cs_wallet);

        CWalletTx *newTx = transaction.getTransaction();
        CWalletTx wtx;


        // Sendmany
        std::vector<std::pair<CScript, int64_t> > vecSend;
        foreach(const SendCoinsRecipient &rcp, recipients)
        {
            std::string sAddr = rcp.address.toStdString();

            if (rcp.typeInd == AddressTableModel::AT_Stealth)
            {
                CStealthAddress sxAddr;
                if (sxAddr.SetEncoded(sAddr))
                {
                    ec_secret ephem_secret;
                    ec_secret secretShared;
                    ec_point pkSendTo;
                    ec_point ephem_pubkey;


                    if (GenerateRandomSecret(ephem_secret) != 0)
                    {
                        printf("GenerateRandomSecret failed.\n");
                        return Aborted;
                    };

                    if (StealthSecret(ephem_secret, sxAddr.scan_pubkey, sxAddr.spend_pubkey, secretShared, pkSendTo) != 0)
                    {
                        printf("Could not generate receiving public key.\n");
                        return Aborted;
                    };

                    CPubKey cpkTo(pkSendTo);
                    if (!cpkTo.IsValid())
                    {
                        printf("Invalid public key generated.\n");
                        return Aborted;
                    };

                    CKeyID ckidTo = cpkTo.GetID();

                    CDigitalNoteAddress addrTo(ckidTo);

                    if (SecretToPublicKey(ephem_secret, ephem_pubkey) != 0)
                    {
                        printf("Could not generate ephem public key.\n");
                        return Aborted;
                    };

                    if (fDebug)
                    {
                        printf("Stealth send to generated pubkey %llu: %s\n", (unsigned long long)pkSendTo.size(), HexStr(pkSendTo).c_str());
                        printf("hash %s\n", addrTo.ToString().c_str());
                        printf("ephem_pubkey %llu: %s\n", (unsigned long long)ephem_pubkey.size(), HexStr(ephem_pubkey).c_str());
                    };

                    CScript scriptPubKey;
                    scriptPubKey.SetDestination(addrTo.Get());

                    vecSend.push_back(std::make_pair(scriptPubKey, rcp.amount));

                    CScript scriptP = CScript() << OP_RETURN << ephem_pubkey;

                    vecSend.push_back(std::make_pair(scriptP, 0));

                    continue;
                } else {
                    printf("Couldn't parse stealth address!\n");
                    return Aborted;
                } // else drop through to normal
            }

            CScript scriptPubKey;
            scriptPubKey.SetDestination(CDigitalNoteAddress(sAddr).Get());
            vecSend.push_back(std::make_pair(scriptPubKey, rcp.amount));
        }

        CReserveKey keyChange(wallet);
        int64_t nFeeRequired = 0;
        int nChangePos = -1;
        std::string strFailReason;

        bool fCreated = wallet->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePos, strFailReason, coinControl, recipients[0].inputType, recipients[0].useInstantX);
        transaction.setTransactionFee(nFeeRequired);

        CAmount nBalance = getBalance(coinControl);

        if(!fCreated)
        {
            if(total > nBalance)
            {
                return AmountExceedsBalance;
            }
            if((total + nFeeRequired) > nBalance) // FIXME: could cause collisions in the future
            {
                return SendCoinsReturn(AmountWithFeeExceedsBalance);
            }
            return TransactionCreationFailed;
        }
        if(!uiInterface.ThreadSafeAskFee(nFeeRequired, tr("Sending...").toStdString()))
        {
            return Aborted;
        }
        if(!wallet->CommitTransaction(wtx, keyChange, (recipients[0].useInstantX) ? "txlreq" : "tx"))
        {
            return TransactionCommitFailed;
        }
    }

    // Add addresses / update labels that we've sent to to the address book
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        std::string strAddress = rcp.address.toStdString();
        CTxDestination dest = CDigitalNoteAddress(strAddress).Get();
        std::string strLabel = rcp.label.toStdString();
        {
            LOCK(wallet->cs_wallet);

            if (rcp.typeInd == AddressTableModel::AT_Stealth)
            {
                wallet->UpdateStealthAddress(strAddress, strLabel, true);
            } else
            {
                mapAddressBook_t::iterator mi = wallet->mapAddressBook.find(dest);

                // Check if we have a new address or an updated label
                if (mi == wallet->mapAddressBook.end() || mi->second != strLabel)
                {
                    wallet->SetAddressBookName(dest, strLabel);
                }
            }
        }
        emit coinsSent(wallet, rcp, transaction_array);
    }
    checkBalanceChanged(); // update balance immediately, otherwise there could be a short noticeable delay until pollBalanceChanged hits

    return SendCoinsReturn(OK);
}

OptionsModel *WalletModel::getOptionsModel()
{
    return optionsModel;
}

AddressTableModel *WalletModel::getAddressTableModel()
{
    return addressTableModel;
}

TransactionTableModel *WalletModel::getTransactionTableModel()
{
    return transactionTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{
    if(!wallet->IsCrypted())
    {
        return Unencrypted;
    }
    else if(wallet->IsLocked())
    {
        return Locked;
    }
    else if(fWalletUnlockStakingOnly)
    {
    return Locked;
    }
    else
    {
        return Unlocked;
    }
}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString &passphrase)
{
    if(encrypted)
    {
        // Encrypt
        return wallet->EncryptWallet(passphrase);
    }
    else
    {
        // Decrypt wallet
        return wallet->DecryptWallet(passphrase);
    }
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase, bool stakingOnly)
{
    if(locked)
    {
        // Lock
        return wallet->Lock();
    }
    else
    {
        // Unlock
        return wallet->Unlock(passPhrase, stakingOnly);
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass)
{
    bool retval;
    {
        LOCK(wallet->cs_wallet);
        wallet->Lock(); // Make sure wallet is locked before attempting pass change
        retval = wallet->ChangeWalletPassphrase(oldPass, newPass);
    }
    return retval;
}

bool WalletModel::backupWallet(const QString &filename)
{
    return BackupWallet(*wallet, filename.toLocal8Bit().data());
}

void WalletModel::checkWallet(int& nMismatchSpent, int64_t& nBalanceInQuestion)
{
	wallet->FixSpentCoins(nMismatchSpent, nBalanceInQuestion, true);
}

void WalletModel::repairWallet(int& nMismatchSpent, int64_t& nBalanceInQuestion)
{
	wallet->FixSpentCoins(nMismatchSpent, nBalanceInQuestion);
}


// Handlers for core signals
static void NotifyKeyStoreStatusChanged(WalletModel *walletmodel, CCryptoKeyStore *wallet)
{
    qDebug() << "NotifyKeyStoreStatusChanged";
    QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
}

static void NotifyAddressBookChanged(WalletModel *walletmodel, CWallet *wallet,
    const CTxDestination &address, const std::string &label, bool isMine, ChangeType status)
{
    if (address.type() == typeid(CStealthAddress))
    {
        CStealthAddress sxAddr = boost::get<CStealthAddress>(address);
        std::string enc = sxAddr.Encoded();
        LogPrintf("NotifyAddressBookChanged %s %s isMine=%i status=%i\n", enc.c_str(), label.c_str(), isMine, status);
        QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(enc)),
                                  Q_ARG(QString, QString::fromStdString(label)),
                                  Q_ARG(bool, isMine),
                                  Q_ARG(int, status));
    } else
    {
    QString strAddress = QString::fromStdString(CDigitalNoteAddress(address).ToString());
    QString strLabel = QString::fromStdString(label);

    qDebug() << "NotifyAddressBookChanged : " + strAddress + " " + strLabel + " isMine=" + QString::number(isMine) + " status=" + QString::number(status);
    QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                              Q_ARG(QString, strAddress),
                              Q_ARG(QString, strLabel),
                              Q_ARG(bool, isMine),
                              Q_ARG(int, status));
    }
}

// queue notifications to show a non freezing progress dialog e.g. for rescan
// queue notifications to show a non freezing progress dialog e.g. for rescan
static bool fQueueNotifications = false;
static std::vector<std::pair<uint256, ChangeType> > vQueueNotifications;
static void NotifyTransactionChanged(WalletModel *walletmodel, CWallet *wallet, const uint256 &hash, ChangeType status)
{
    if (fQueueNotifications)
    {
        vQueueNotifications.push_back(std::make_pair(hash, status));
        return;
    }

    QString strHash = QString::fromStdString(hash.GetHex());

    qDebug() << "NotifyTransactionChanged : " + strHash + " status= " + QString::number(status);
    QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection/*,
                              Q_ARG(QString, strHash),
                              Q_ARG(int, status)*/);
}

static void ShowProgress(WalletModel *walletmodel, const std::string &title, int nProgress)
{
    // emits signal "showProgress"
    QMetaObject::invokeMethod(walletmodel, "showProgress", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(title)),
                              Q_ARG(int, nProgress));
    if (nProgress == 0)
    {
    	fQueueNotifications = true;
        // Set the batch flag synchronously (not via QueuedConnection)
        // because transactiontablemodel's boost handler -- which fires
        // first in registration order -- queues table updates that
        // ultimately trigger incomingTransaction.  If we deferred the
        // flag set, those incomingTransaction calls would see flag=false
        // and fire per-tx toasts.  Synchronous set via the public
        // accessor; bool write is atomic enough for our purposes.
        walletmodel->setProcessingQueuedTransactions(true);
    }

    if (nProgress == 100)
    {
        fQueueNotifications = false;
        // A9: keep fProcessingQueuedTransactions=true for the entire
        // drain so that incomingTransaction (in bitcoingui.cpp) treats
        // every drained tx as part of the batch.  The single summary
        // toast for the whole batch is fired from
        // DigitalNoteGUI::showProgress(100) -> maybeEmitBatchSummary.
        for (unsigned int i = 0; i < vQueueNotifications.size(); ++i)
        {
            NotifyTransactionChanged(walletmodel, NULL, vQueueNotifications[i].first, vQueueNotifications[i].second);
        }
        std::vector<std::pair<uint256, ChangeType> >().swap(vQueueNotifications); // clear
        // Drain complete -- restore the flag via QueuedConnection so
        // the unset happens AFTER all the queued updateTransaction
        // events (and their cascading rowsInserted -> incomingTransaction
        // chain) have been processed on the main thread.  If we unset
        // synchronously here, real-time txs arriving immediately after
        // would race with the still-draining queue.
        QMetaObject::invokeMethod(walletmodel, "setProcessingQueuedTransactions", Qt::QueuedConnection, Q_ARG(bool, false));
    }
}

static void NotifyWatchonlyChanged(WalletModel *walletmodel, bool fHaveWatchonly)
{
    QMetaObject::invokeMethod(walletmodel, "updateWatchOnlyFlag", Qt::QueuedConnection,
                              Q_ARG(bool, fHaveWatchonly));
}

void WalletModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyStatusChanged.connect(
		boost::bind(
			&NotifyKeyStoreStatusChanged,
			this,
			boost::placeholders::_1
		)
	);
    
	wallet->NotifyAddressBookChanged.connect(
		boost::bind(
			&NotifyAddressBookChanged,
			this,
			boost::placeholders::_1,
			boost::placeholders::_2,
			boost::placeholders::_3,
			boost::placeholders::_4,
			boost::placeholders::_5
		)
	);
	
    wallet->NotifyTransactionChanged.connect(
		boost::bind(
			&NotifyTransactionChanged,
			this,
			boost::placeholders::_1,
			boost::placeholders::_2,
			boost::placeholders::_3
		)
	);
    
	wallet->ShowProgress.connect(
		boost::bind(
			&ShowProgress,
			this,
			boost::placeholders::_1,
			boost::placeholders::_2
		)
	);
	
    wallet->NotifyWatchonlyChanged.connect(
		boost::bind(
			&NotifyWatchonlyChanged,
			this,
			boost::placeholders::_1
		)
	);
}

void WalletModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyStatusChanged.disconnect(
		boost::bind(
			&NotifyKeyStoreStatusChanged,
			this,
			boost::placeholders::_1
		)
	);
	
    wallet->NotifyAddressBookChanged.disconnect(
		boost::bind(
			&NotifyAddressBookChanged,
			this,
			boost::placeholders::_1,
			boost::placeholders::_2,
			boost::placeholders::_3,
			boost::placeholders::_4,
			boost::placeholders::_5
		)
	);
	
    wallet->NotifyTransactionChanged.disconnect(
		boost::bind(
			&NotifyTransactionChanged,
			this,
			boost::placeholders::_1,
			boost::placeholders::_2,
			boost::placeholders::_3
		)
	);
	
    wallet->ShowProgress.disconnect(
		boost::bind(
			&ShowProgress,
			this,
			boost::placeholders::_1,
			boost::placeholders::_2
		)
	);
	
    wallet->NotifyWatchonlyChanged.disconnect(
		boost::bind(
			&NotifyWatchonlyChanged,
			this,
			boost::placeholders::_1
		)
	);
}

// WalletModel::UnlockContext implementation



bool WalletModel::generateMnemonic(BIP39Wallet::WordCount wordCount,
                                   SecureString &mnemonic) const
{
    BIP39Wallet::Result res = BIP39Wallet::generateMnemonic(
        *wallet, wordCount, mnemonic);
    return res == BIP39Wallet::Result::OK;
}

bool WalletModel::hasRecoveryPhraseSupport() const
{
    return wallet->HasRecoveryPhraseFlag();
}

bool WalletModel::needsRecoveryPhraseUpgrade() const
{
    // Decision is made in the Qt layer rather than CWallet because the
    // "declined" half is a UI preference stored in QSettings, not wallet
    // data.  CWallet exposes only the two cryptographic facts we need:
    // is the wallet encrypted, and does it already have CMasterKey[2].
    if (!wallet->IsCrypted())          return false;
    if (wallet->HasMnemonicMasterKey()) return false;

    // Per-wallet dismissal lookup -- key is the absolute wallet path,
    // hashed inside GuiState for QSettings-key safety.
    const std::string walletPath =
        (GetDataDir() / boost::filesystem::path(wallet->strWalletFile)).string();
    if (GuiState::isRecoveryPhraseUpgradeDeclined(walletPath)) return false;

    return true;
}

void WalletModel::setRecoveryPhraseUpgradeDeclined()
{
    // Per-wallet dismissal: identifies this specific wallet file by its
    // full path and records the dismissal in QSettings, not wallet.dat.
    // A different wallet file (or the same wallet at a different
    // location) gets prompted afresh.
    const std::string walletPath =
        (GetDataDir() / boost::filesystem::path(wallet->strWalletFile)).string();
    GuiState::setRecoveryPhraseUpgradeDeclined(walletPath);
}

bool WalletModel::hasMnemonicMasterKey() const
{
    return wallet->HasMnemonicMasterKey();
}

bool WalletModel::addMnemonicMasterKey()
{
    // D2: no passphrase arg.  The mnemonic is derived from vMasterKey,
    // which the unlocked wallet already has in memory.
    return wallet->AddMnemonicMasterKey();
}

bool WalletModel::removeMnemonicMasterKey()
{
    return wallet->RemoveMnemonicMasterKey();
}

bool WalletModel::verifyPassphrase(const SecureString &passphrase) const
{
    return wallet->VerifyPassphrase(passphrase);
}

bool WalletModel::getCurrentMnemonic(SecureString &mnemonicOut) const
{
    // Wallet must be unlocked for this to work.  We do NOT auto-unlock --
    // the caller is responsible for prompting the user (via the existing
    // AskPassphraseDialog flow) before invoking this.
    return wallet->GetCurrentMnemonic(mnemonicOut);
}

bool WalletModel::rotateRecoveryPhrase(const SecureString &currentPassword,
                                        SecureString &newMnemonicOut)
{
    // Caller must have shown the wall-of-text consent dialog and obtained
    // explicit user agreement before reaching here.  We just plumb through
    // to CWallet::RotateMnemonicMasterKey which does the heavy lifting.
    return wallet->RotateMnemonicMasterKey(currentPassword, newMnemonicOut);
}

WalletModel::UnlockContext WalletModel::requestUnlockWithMnemonic(const QString &mnemonic)
{
    bool was_locked = wallet->IsLocked();

    // Validate then derive the passphrase from the mnemonic entropy
    SecureString mnemonicSS;
    std::string mnStr = mnemonic.simplified().trimmed().toStdString();
    mnemonicSS.assign(mnStr.c_str(), mnStr.size());

    if (!BIP39Wallet::validateMnemonic(mnemonicSS))
        return UnlockContext(this, false, false);

    SecureString derivedPass;
    BIP39Passphrase::Result res = BIP39Passphrase::passphraseFromMnemonic(mnemonicSS, derivedPass);
    if (res != BIP39Passphrase::Result::OK)
        return UnlockContext(this, false, false);

    bool valid = !was_locked || wallet->Unlock(derivedPass);

    // relock=false — user chose to unlock with phrase, wallet stays unlocked
    return UnlockContext(this, valid, false);
}

WalletModel::UnlockContext WalletModel::requestUnlock()
{
    bool was_locked = getEncryptionStatus() == Locked;

    if ((!was_locked) && fWalletUnlockStakingOnly)
    {
       setWalletLocked(true);
       was_locked = getEncryptionStatus() == Locked;

    }
    if(was_locked)
    {
        // Request UI to unlock wallet
        emit requireUnlock();
    }
    // If wallet is still locked, unlock was failed or cancelled, mark context as invalid
    bool valid = getEncryptionStatus() != Locked;

    return UnlockContext(this, valid, was_locked && !fWalletUnlockStakingOnly);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *wallet, bool valid, bool relock):
        wallet(wallet),
        valid(valid),
        relock(relock)
{
}

WalletModel::UnlockContext::~UnlockContext()
{
    if(valid && relock)
    {
        wallet->setWalletLocked(true);
    }
}

void WalletModel::UnlockContext::CopyFrom(const UnlockContext& rhs)
{
    // DigitalNote context; old object no longer relocks wallet
    *this = rhs;
    rhs.relock = false;
}

bool WalletModel::getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    return wallet->GetPubKey(address, vchPubKeyOut);
}

// returns a list of COutputs from COutPoints
void WalletModel::getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs)
{
    LOCK2(cs_main, wallet->cs_wallet);
    
	for(const COutPoint& outpoint : vOutpoints)
    {
        if (!wallet->mapWallet.count(outpoint.hash)) continue;
        int nDepth = wallet->mapWallet[outpoint.hash].GetDepthInMainChain();
        if (nDepth < 0) continue;
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth, true);
        vOutputs.push_back(out);
    }
}

// AvailableCoins + LockedCoins grouped by wallet address (put change in one group with wallet address)
void WalletModel::listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const
{

    std::vector<COutput> vCoins;
    wallet->AvailableCoins(vCoins);

    LOCK2(cs_main, wallet->cs_wallet); // ListLockedCoins, mapWallet
    std::vector<COutPoint> vLockedCoins;
    wallet->ListLockedCoins(vLockedCoins);

    // add locked coins
    for(const COutPoint& outpoint : vLockedCoins)
    {
        if (!wallet->mapWallet.count(outpoint.hash)) continue;
        int nDepth = wallet->mapWallet[outpoint.hash].GetDepthInMainChain();
        if (nDepth < 0) continue;
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth, true);
        if (outpoint.n < out.tx->vout.size() && wallet->IsMine(out.tx->vout[outpoint.n]) == ISMINE_SPENDABLE)
            vCoins.push_back(out);
    }

    for(const COutput& out : vCoins)
    {
        COutput cout = out;

        while (wallet->IsChange(cout.tx->vout[cout.i]) && cout.tx->vin.size() > 0 && wallet->IsMine(cout.tx->vin[0]))
        {
            if (!wallet->mapWallet.count(cout.tx->vin[0].prevout.hash)) break;
            cout = COutput(&wallet->mapWallet[cout.tx->vin[0].prevout.hash], cout.tx->vin[0].prevout.n, 0, true);
        }

        CTxDestination address;
        if(!out.fSpendable || !ExtractDestination(cout.tx->vout[cout.i].scriptPubKey, address))
            continue;
        mapCoins[QString::fromStdString(CDigitalNoteAddress(address).ToString())].push_back(out);
    }
}

bool WalletModel::isLockedCoin(uint256 hash, unsigned int n) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    return wallet->IsLockedCoin(hash, n);
}

void WalletModel::lockCoin(COutPoint& output)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->LockCoin(output);
}

void WalletModel::unlockCoin(COutPoint& output)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->UnlockCoin(output);
}

void WalletModel::listLockedCoins(std::vector<COutPoint>& vOutpts)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->ListLockedCoins(vOutpts);
}

void WalletModel::listLockedOutputsWithDetails(std::vector<LockedOutputDetail>& vDetails)
{
    vDetails.clear();

    // Snapshot the lock set and joined wallet state under cs_wallet.
    // mapAddressBook, mapWallet, and setLockedCoins are all wallet
    // state, so we hold one lock for all of them.
    std::vector<COutPoint> vOutpts;
    {
        LOCK2(cs_main, wallet->cs_wallet);
        wallet->ListLockedCoins(vOutpts);

        // Build a lookup of MN-collateral txid:vout -> alias.  We could
        // walk masternodeConfig for every locked output but that's
        // O(N*M) for N locks and M MN entries; the lookup keeps it O(N+M).
        std::map<std::pair<uint256, int>, std::string> mnByOutpoint;
        for (const CMasternodeConfigEntry& mne : masternodeConfig.getEntries())
        {
            uint256 mnHash;
            mnHash.SetHex(mne.getTxHash());
            int mnVout = atoi(mne.getOutputIndex().c_str());
            mnByOutpoint[std::make_pair(mnHash, mnVout)] = mne.getAlias();
        }

        const int64_t mnCollateralSatoshis =
            MasternodeCollateral(pindexBest ? pindexBest->nHeight : 0) * COIN;

        for (const COutPoint& out : vOutpts)
        {
            // Defensive: tx not in mapWallet means the locked output
            // refers to a transaction we don't know about (reorg,
            // database inconsistency).  Skip rather than show a
            // half-populated row -- the user can clean up via RPC if
            // they want, but we don't want to be the one rendering
            // garbage.
            auto txIt = wallet->mapWallet.find(out.hash);
            if (txIt == wallet->mapWallet.end())
                continue;
            const CWalletTx& wtx = txIt->second;
            if (out.n >= wtx.vout.size())
                continue;

            // Watch-only filter: skip outputs we can't actually spend.
            // The user can manage those on the wallet that owns the
            // spend key, not here.
            isminetype mine = wallet->IsMine(wtx.vout[out.n]);
            if ((mine & ISMINE_SPENDABLE) == 0)
                continue;

            LockedOutputDetail detail;
            detail.outpoint = out;
            detail.amount = wtx.vout[out.n].nValue;
            detail.tier = LockedOutputDetail::LOT_OTHER;

            // Address + address-book label
            CTxDestination dest;
            if (ExtractDestination(wtx.vout[out.n].scriptPubKey, dest))
            {
                detail.address = QString::fromStdString(CDigitalNoteAddress(dest).ToString());
                auto abIt = wallet->mapAddressBook.find(dest);
                if (abIt != wallet->mapAddressBook.end())
                    detail.addressLabel = QString::fromStdString(abIt->second);
            }

            // Classify
            auto mnIt = mnByOutpoint.find(std::make_pair(out.hash, static_cast<int>(out.n)));
            if (mnIt != mnByOutpoint.end())
            {
                detail.tier = LockedOutputDetail::LOT_MASTERNODE;
                detail.masternodeAlias = QString::fromStdString(mnIt->second);
            }
            else if (detail.amount == mnCollateralSatoshis)
            {
                detail.tier = LockedOutputDetail::LOT_MN_COLLATERAL_AMOUNT;
            }
            // else stays LOT_OTHER

            vDetails.push_back(detail);
        }
    }

    // Sort so most-consequential first: configured MNs, then matching
    // amount but unconfigured, then other locks.  Within a tier, sort
    // by alias/address for stability.
    std::sort(vDetails.begin(), vDetails.end(),
        [](const LockedOutputDetail& a, const LockedOutputDetail& b)
        {
            if (a.tier != b.tier)
                return a.tier < b.tier;
            if (a.tier == LockedOutputDetail::LOT_MASTERNODE)
                return a.masternodeAlias < b.masternodeAlias;
            return a.address < b.address;
        });
}

void WalletModel::emitCollateralCandidate(const QString &txidHex, int vout)
{
    // Trampoline: TransactionTablePriv can't emit directly because it is
    // not a QObject subclass.  This is invoked from the CT_NEW handling
    // path with the wallet locks already released.
    emit collateralCandidateReceived(txidHex, vout);
}