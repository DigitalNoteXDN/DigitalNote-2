#ifndef BITCOINGUI_H
#define BITCOINGUI_H

#include <QMainWindow>
#include <QSystemTrayIcon>

#include <stdint.h>

class TransactionTableModel;
class ClientModel;
class WalletModel;
class TransactionView;
class OverviewPage;
class AddressBookPage;
class SendCoinsDialog;
class SignVerifyMessageDialog;
class Notificator;
class RPCConsole;
class MasternodeManager;
class MessagePage;
class MessageModel;
class BlockBrowser;
class SeedPhraseDialog;

QT_BEGIN_NAMESPACE
class QLabel;
class QModelIndex;
class QProgressBar;
class QProgressDialog;
class QStackedWidget;
class QScrollArea;
QT_END_NAMESPACE

/**
  DigitalNote GUI main class. This class represents the main window of the DigitalNote UI. It communicates with both the client and
  wallet models to give the user an up-to-date view of the current core state.
*/
class DigitalNoteGUI : public QMainWindow
{
    Q_OBJECT

public:
    explicit DigitalNoteGUI(QWidget *parent = 0);
    ~DigitalNoteGUI();

    /** Set the client model.
        The client model represents the part of the core that communicates with the P2P network, and is wallet-agnostic.
    */
    void setClientModel(ClientModel *clientModel);
    /** Set the wallet model.
        The wallet model represents a bitcoin wallet, and offers access to the list of transactions, address book and sending
        functionality.
    */
    void setWalletModel(WalletModel *walletModel);
    /** Set the message model.
        The message model represents a DigitalNote  Note or D-Note, and offers secure messaging through a peer to peer
        relay.
    */
    void setMessageModel(MessageModel *messageModel);

protected:
    void changeEvent(QEvent *e);
    void closeEvent(QCloseEvent *event);
    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);

private:
    ClientModel *clientModel;
    WalletModel *walletModel;
    MessageModel *messageModel;

    QToolBar *toolbar;

    QStackedWidget *centralStackedWidget;

    QWidget *overviewWidget;
    QScrollArea *overviewScroll;
    OverviewPage *overviewPage;
    QWidget *transactionsPage;
    AddressBookPage *addressBookPage;
    AddressBookPage *receiveCoinsPage;
    SendCoinsDialog *sendCoinsPage;
    SignVerifyMessageDialog *signVerifyMessageDialog;
    MasternodeManager *masternodeManagerPage;
    MessagePage *messagePage;
    QLabel* netLabel;
    BlockBrowser *blockBrowser;
    QLabel *labelEncryptionIcon;
    QLabel *labelStakingIcon;
    QLabel *labelConnectionsIcon;
    QLabel *labelBlocksIcon;
    QLabel *progressBarLabel;
    QProgressBar *progressBar;
    QProgressDialog *progressDialog;

    QMenuBar *appMenuBar;
    QAction *overviewAction;
    QAction *historyAction;
    QAction *quitAction;
    QAction *sendCoinsAction;
    QAction *addressBookAction;
    QAction *signMessageAction;
    QAction *verifyMessageAction;
    QAction *aboutAction;
    QAction *receiveCoinsAction;
    QAction *optionsAction;
    QAction *toggleHideAction;
    QAction *exportAction;
    QAction *encryptWalletAction;
    QAction *backupWalletAction;
    QAction *importPrivateKeyAction;
    QAction *changePassphraseAction;
    QAction *unlockForStakingAction;
    QAction *unlockWalletAction;
    QAction *lockWalletAction;
	QAction *checkWalletAction;
	QAction *repairWalletAction;
    QAction *compactWalletAction;
    QAction *seedPhraseAction;
    QAction *aboutQtAction;
    QAction *openRPCConsoleAction;
    QAction *masternodeManagerAction;
    QAction *messageAction;
    QAction *blockAction;
    QAction *showBackupsAction;
    QAction *editConfigAction;
    QAction *editConfigExtAction;
    QAction *openDataDirAction;
    QAction *lockedOutputsAction;

    QSystemTrayIcon *trayIcon;
    Notificator *notificator;
    TransactionView *transactionView;
    RPCConsole *rpcConsole;
    SeedPhraseDialog *seedPhraseDialog;

    QMovie *syncIconMovie;
    /** Keep track of previous number of blocks, to detect progress */
    int prevBlocks;

    uint64_t nWeight;

    // v2.0.0.8 CW2: staking-icon state machine.
    //
    // m_bHammerLatched: once updateStakingIcon() resolves the icon to
    // Hammer via the full Phase-A prerequisite walk, latch this flag.
    // While latched, only the invalidating-events set + a 5-minute
    // staleness floor can drop us off Hammer; transient §29 defers do
    // not flutter the icon.  Single-threaded (only the GUI main thread
    // touches it), so no atomic.
    mutable bool m_bHammerLatched;

    enum class StakingIconState { None, Hammer, Clock };

    // Phase-A walk: full prerequisite check including loop-productivity.
    StakingIconState ComputeStakingIconStatePhaseA(
        bool fIBD, bool fWalletLocked, QString &tooltipOut) const;

    // Phase-B walk: invalidating-events only, plus the 5-minute floor.
    StakingIconState ComputeStakingIconStatePhaseB(
        bool fIBD, bool fWalletLocked, QString &tooltipOut) const;

    // Tooltip-detail computation for the Hammer state -- expected
    // time-between-blocks from nWeight + chain difficulty.
    QString ComputeHammerTooltip() const;

    // A9: count of incoming-tx notifications suppressed during the
    // current batch period (IBD/catchup or explicit rescan import).
    // When the batch ends, we fire ONE summary toast naming the count
    // and the kind, instead of the individual per-tx toasts that were
    // suppressed.
    int nBatchTxCount;
    bool fInBatchMode;
    enum BatchKind { BATCH_NONE, BATCH_SYNC, BATCH_IMPORT };
    BatchKind eBatchKind;

    /** Create the main UI actions. */
    void createActions();
    /** Create the menu bar and sub-menus. */
    void createMenuBar();
    /** Create the toolbars */
    void createToolBars();
    /** Create system tray (notification) icon */
    void createTrayIcon();

    void clearWidgets();

public slots:
    /** Set number of connections shown in the UI */
    void setNumConnections(int count);
    /** Set number of blocks shown in the UI */
    void setNumBlocks(int count);
    /** Set the encryption status as shown in the UI.
       @param[in] status            current encryption status
       @see WalletModel::EncryptionStatus
    */
    void setEncryptionStatus(int status);
    void onRecoveryPhraseUpgradeAvailable();

    /** Notify the user of an error in the network or transaction handling code. */
    void error(const QString &title, const QString &message, bool modal);
    /** Notify the user of an event from the core network or transaction handling code.
       @param[in] title     the message box / notification title
       @param[in] message   the displayed text
       @param[in] modal     true to use a message box, false to use a notification
       @param[in] style     style definitions (icon and used buttons - buttons only for message boxes)
                            @see CClientUIInterface::MessageBoxFlags
    */
    void message(const QString &title, const QString &message, bool modal, unsigned int style);
    /** Asks the user whether to pay the transaction fee or to cancel the transaction.
       It is currently not possible to pass a return value to another thread through
       BlockingQueuedConnection, so an indirected pointer is used.
       https://bugreports.qt-project.org/browse/QTBUG-10440

      @param[in] nFeeRequired       the required fee
      @param[out] payFee            true to pay the fee, false to not pay the fee
    */
    void askFee(qint64 nFeeRequired, bool *payFee);
    void handleURI(QString strURI);

private slots:
    /** Switch to overview (home) page */
    void gotoOverviewPage();
    /** Switch to history (transactions) page */
    void gotoHistoryPage();
    /** Switch to address book page */
    void gotoAddressBookPage();
    /** Switch to receive coins page */
    void gotoReceiveCoinsPage();
    /** Switch to send coins page */
    void gotoSendCoinsPage();
    /** Switch to block explorer*/
    void gotoBlockBrowser();
    /** Switch to masternode manager page*/
    void gotoMasternodeManagerPage();
    /** Show Sign/Verify Message dialog and switch to sign message tab */
    void gotoSignMessageTab(QString addr = "");
    /** Show Sign/Verify Message dialog and switch to verify message tab */
    void gotoVerifyMessageTab(QString addr = "");
    /** Switch to message page*/
    void gotoMessagePage();
    /** Show configuration dialog */
    void optionsClicked();
    /** Show about dialog */
    void aboutClicked();
    
#ifndef Q_OS_MAC
    /** Handle tray icon clicked */
    void trayIconActivated(QSystemTrayIcon::ActivationReason reason);
#endif
    /** Show incoming transaction notification for new transactions.
        The new items are those between start and end inclusive, under the given parent item.
    */
    void incomingTransaction(const QModelIndex & parent, int start, int end);
    /** B1: prompt the user when an incoming transaction creates a fresh
        masternode-collateral-shaped UTXO (2,000,000 XDN, spendable,
        not already locked, not globally suppressed for this wallet).
        Emitted from TransactionTablePriv via WalletModel after CT_NEW. */
    void onCollateralCandidateReceived(const QString &txidHex, int vout);
    /** A9: emit a single summary toast naming nBatchTxCount and the
        recently-finished batch kind, then reset batch state. Called
        from setNumBlocks() (for IBD-end) and showProgress(100) (for
        explicit-batch-end). */
    void maybeEmitBatchSummary();
    /** Show incoming D-Note receipt notification for new secure messages.
        The new items are those between start and end inclusive, under the given parent item.
    */
    void incomingMessage(const QModelIndex & parent, int start, int end);
    /** Encrypt the wallet */
    void encryptWallet();
	/** Check the wallet */
    void checkWallet();
    /** Repair the wallet */
    void repairWallet();
    /** Compact (rebuild) the wallet via the maintenance-mode rebuildwallet
     *  pipeline.  Shows a confirmation dialog explaining that the wallet
     *  will restart and the original is preserved as wallet.dat.bak; on
     *  confirm, writes the .rebuildwallet-pending flag file and requests
     *  app shutdown.  Next launch picks up the flag and runs RebuildWallet
     *  before LoadWallet. */
    void compactWallet();
    /** Tools -> Locked Outputs...
     *  Opens the modal Locked Outputs dialog which lists every output
     *  currently held in setLockedCoins (filtered to spendable-by-this-
     *  wallet) with three-tier classification (configured masternode /
     *  2M XDN-not-configured / other) and per-row toggle with tier-
     *  appropriate confirmations. */
    void showLockedOutputs();
    /** On first paint after launch, check for a .rebuildwallet-result
     *  marker (written by the AppInit2 rebuild handler) and surface the
     *  outcome to the user via a single one-shot dialog.  The marker is
     *  deleted after consumption so the dialog never re-fires.  Wired
     *  via QTimer::singleShot(0,...) from setClientModel so it runs once
     *  the event loop is alive but before any user interaction. */
    void showRebuildResultIfPresent();
	/** Backup the wallet */
    void backupWallet();
    /** Import a private key */
    void importPrivateKey();
    /** Change encrypted wallet passphrase */
    void changePassphrase();
    /** Ask for passphrase to unlock wallet just for staking */
    void unlockForStaking();
    /** Ask for passphrase to unlock wallet temporarily */
    void unlockWallet();

    void lockWallet();

    /** Show window if hidden, unminimize when minimized, rise when obscured or show if hidden and fToggleHidden is true */
    void showNormalIfMinimized(bool fToggleHidden = false);
    /** simply calls showNormalIfMinimized(true) for use in SLOT() macro */
    void toggleHidden();

    void updateWeight();
    void updateStakingIcon();

    /** called by a timer to check if fRequestShutdown has been set **/
    void detectShutdown();

    /** Show progress dialog e.g. for verifychain */
    void showProgress(const QString &title, int nProgress);

    /** Edit the DigitalNote.conf file */
    void editConfig();
    void editConfigExt();
    /** Open the data directory */
    void openDataDir();
    void showSeedPhrase();
};

#endif // BITCOINGUI_H