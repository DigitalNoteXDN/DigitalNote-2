/*
 * Qt5 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The DigitalNote Developers 2018-2026
 */

#include "compat.h"

#include <cmath>
#include <iostream>

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QIcon>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QMimeData>
#include <QProgressBar>
#include <QProgressDialog>
#include <QStackedWidget>
#include <QDateTime>
#include <QMovie>
#include <QFileDialog>
#include <QDesktopServices>
#include <QTimer>
#include <QDragEnterEvent>
#include <QUrl>
#include <QMimeData>
#include <QStyle>
#include <QToolButton>
#include <QScrollArea>
#include <QScroller>
#include <QTextDocument>
#include <QInputDialog>

#include <boost/filesystem/path.hpp>

#include "transactiontablemodel.h"
#include "addressbookpage.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "editaddressdialog.h"
#include "editconfigdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "addresstablemodel.h"
#include "transactionview.h"
#include "overviewpage.h"
#include "bitcoinunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "recoveryphraseupgradedialog.h"
#include "notificator.h"
#include "guiutil.h"
#include "guistate.h"
#include "rpcconsole.h"
#include "init.h"
#include "masternodemanager.h"
#include "messagemodel.h"
#include "messagepage.h"
#include "blockbrowser.h"
#include "importprivatekeydialog.h"
#include "cblock.h"
#include "mining.h"
#include "wallet.h"
#include "net.h"
#include "cscript.h"
#include "coutpoint.h"
#include "main_extern.h"
#include "thread.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "cclientuiinterface.h"
#include "bitcoinunits.h"
#include "seedphrasedialog.h"
#include "walletrebuild.h"
#include "lockedoutputsdialog.h"
#include "util.h"   // for LogPrintf

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include "bitcoingui.h"

extern bool fOnlyTor;
extern CWallet* pwalletMain;
extern int64_t nLastCoinStakeSearchInterval;
double GetPoSKernelPS();

// v2.0.0.8 CW2: published miner-thread state for staking-icon state machine.
// Defined in miner.cpp.  Read without lock (std::atomic).
#include <atomic>
extern std::atomic<int64_t> nLastStakeLoopTime;
extern std::atomic<bool> fLastStakeLoopProductive;

// File-local freshness windows for the staking-icon state machine.
static constexpr int64_t STAKE_LOOP_FRESHNESS_SECS_INITIAL = 30;
static constexpr int64_t STAKE_LOOP_FRESHNESS_SECS_LATCHED = 5 * 60;
bool fGUIunlock;

DigitalNoteGUI::DigitalNoteGUI(QWidget *parent):
    QMainWindow(parent),
    clientModel(0),
    walletModel(0),
    toolbar(0),
    progressBarLabel(0),
    progressBar(0),
    progressDialog(0),
    encryptWalletAction(0),
    changePassphraseAction(0),
    unlockForStakingAction(0),
    unlockWalletAction(0),
    lockWalletAction(0),
    aboutQtAction(0),
    trayIcon(0),
    notificator(0),
    rpcConsole(0),
    prevBlocks(0),
    nWeight(0),
    m_bHammerLatched(false),
    seedPhraseDialog(0),
    nBatchTxCount(0),
    fInBatchMode(false),
    eBatchKind(BATCH_NONE)
{
    resize(900, 520);
    setWindowTitle(tr("DigitalNote") + " - " + tr("Wallet"));
#ifndef Q_OS_MAC
    qApp->setWindowIcon(QIcon(":/icons/bitcoin"));
    setWindowIcon(QIcon(":/icons/bitcoin"));
#else
    //setUnifiedTitleAndToolBarOnMac(true);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif
    setObjectName("DigitalNote");
    setStyleSheet("#DigitalNote { background-color: #ffffff; color: #614eb0;}");

    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create the tray icon (or setup the dock icon)
    createTrayIcon();

    // Create tabs
    overviewPage = new OverviewPage();

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    transactionsPage->setLayout(vbox);

    blockBrowser = new BlockBrowser(this);

    addressBookPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab);

    receiveCoinsPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab);

    sendCoinsPage = new SendCoinsDialog(this);

    signVerifyMessageDialog = new SignVerifyMessageDialog(this);

    masternodeManagerPage = new MasternodeManager(this);

    messagePage = new MessagePage(this);

    centralStackedWidget = new QStackedWidget(this);
    centralStackedWidget->setContentsMargins(0, 0, 0, 0);
    centralStackedWidget->addWidget(overviewPage);
    centralStackedWidget->addWidget(transactionsPage);
    centralStackedWidget->addWidget(addressBookPage);
    centralStackedWidget->addWidget(receiveCoinsPage);
    centralStackedWidget->addWidget(sendCoinsPage);
    centralStackedWidget->addWidget(masternodeManagerPage);
    centralStackedWidget->addWidget(messagePage);
    centralStackedWidget->addWidget(blockBrowser);

    QWidget *centralWidget = new QWidget();
    QVBoxLayout *centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->setContentsMargins(0,0,0,0);
    centralWidget->setContentsMargins(0,0,0,0);
    centralLayout->addWidget(centralStackedWidget);

    setCentralWidget(centralWidget);

    // Create status bar
    statusBar();

    // Disable size grip because it looks ugly and nobody needs it
    statusBar()->setSizeGripEnabled(false);

    // Status bar notification icons
    QWidget *frameBlocks = new QWidget();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    frameBlocks->setStyleSheet("QWidget { background: none; margin-bottom: 5px; }");
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    frameBlocksLayout->setAlignment(Qt::AlignHCenter);
    labelEncryptionIcon = new QLabel();
    labelStakingIcon = new QLabel();
    labelConnectionsIcon = new QLabel();
    labelBlocksIcon = new QLabel();
    frameBlocksLayout->addWidget(netLabel);
    //frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelEncryptionIcon);
    //frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelStakingIcon);
    //frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelConnectionsIcon);
    //frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    //frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(netLabel);
    //frameBlocksLayout->addStretch();


    if (GetBoolArg("-staking", true))
    {
        QTimer *timerStakingIcon = new QTimer(labelStakingIcon);
        connect(timerStakingIcon, SIGNAL(timeout()), this, SLOT(updateStakingIcon()));
        timerStakingIcon->start(20 * 1000);
        updateStakingIcon();
    }

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new QProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    if (!fUseDarkTheme)
    {
        // Override style sheet for progress bar for styles that have a segmented progress bar,
        // as they make the text unreadable (workaround for issue #1071)
        // See https://qt-project.org/doc/qt-4.8/gallery.html
        QString curStyle = qApp->style()->metaObject()->className();
        if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
        {
            progressBar->setStyleSheet("QProgressBar { color: #ffffff;background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #eb1f24, stop: 1 #4c5259); border-radius: 7px; margin: 0px; }");
        }
    }

    // Layout: [label fixed] [bar expanding] [icons fixed]
    // label: stretch=0 (natural width)
    // bar:   stretch=1 (takes all remaining space)
    // frameBlocks: permanentWidget (natural width, right-anchored)
    statusBar()->addWidget(progressBarLabel, 0);
    statusBar()->addWidget(progressBar, 1);
    statusBar()->addPermanentWidget(frameBlocks);
    statusBar()->setObjectName("statusBar");
    statusBar()->setStyleSheet("#statusBar { color: #3098c6; background-color: #1d1f22; }");

    if (!fUseDarkTheme)
    {
        statusBar()->setStyleSheet("#statusBar { color: #ffffff; background-color: #614eb0; }");
    }

    syncIconMovie = new QMovie(":/movies/update_spinner", "mng", this);

    // Clicking on a transaction on the overview page simply sends you to transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), this, SLOT(gotoHistoryPage()));
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    rpcConsole = new RPCConsole(0);
    connect(openRPCConsoleAction, SIGNAL(triggered()), rpcConsole, SLOT(show()));

    // clicking on automatic backups shows details
    connect(showBackupsAction, SIGNAL(triggered()), rpcConsole, SLOT(showBackups()));

    // prevents an oben debug window from becoming stuck/unusable on client shutdown
    connect(quitAction, SIGNAL(triggered()), rpcConsole, SLOT(hide()));

    // Clicking on "Verify Message" in the address book sends you to the verify message tab
    connect(addressBookPage, SIGNAL(verifyMessage(QString)), this, SLOT(gotoVerifyMessageTab(QString)));
    // Clicking on "Sign Message" in the receive coins page sends you to the sign message tab
    connect(receiveCoinsPage, SIGNAL(signMessage(QString)), this, SLOT(gotoSignMessageTab(QString)));

    gotoOverviewPage();
}

DigitalNoteGUI::~DigitalNoteGUI()
{
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
#endif

    delete rpcConsole;
}

void DigitalNoteGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(QIcon(":/icons/overview"), tr("&Dashboard"), this);
    overviewAction->setToolTip(tr("Show general overview of wallet"));
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses"), tr("&Receive"), this);
    receiveCoinsAction->setToolTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(receiveCoinsAction);

    sendCoinsAction = new QAction(QIcon(":/icons/send-sidebar"), tr("&Send"), this);
    sendCoinsAction->setToolTip(tr("Send coins to a DigitalNote address"));
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(sendCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history"), tr("&Transactions"), this);
    historyAction->setToolTip(tr("Browse transaction history"));
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book-sidebar"), tr("&Addresses"), this);
    addressBookAction->setToolTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(addressBookAction);

    masternodeManagerAction = new QAction(QIcon(":/icons/mnodes"), tr("&Masternodes"), this);
    masternodeManagerAction->setToolTip(tr("Show Master Nodes status and configure your nodes."));
    masternodeManagerAction->setCheckable(true);
    tabGroup->addAction(masternodeManagerAction);

    messageAction = new QAction(QIcon(":/icons/message"), tr("&Messages"), this);
    messageAction->setToolTip(tr("View and Send Encrypted messages"));
    messageAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_7));
    messageAction->setCheckable(true);
    tabGroup->addAction(messageAction);

    blockAction = new QAction(QIcon(":/icons/block"), tr("&Block Explorer"), this);
    blockAction->setToolTip(tr("Explore the BlockChain"));
    blockAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
    blockAction->setCheckable(true);
    tabGroup->addAction(blockAction);

    showBackupsAction = new QAction(QIcon(":/icons/browse"), tr("Show Auto&Backups"), this);
    showBackupsAction->setStatusTip(tr("S"));

    connect(blockAction, SIGNAL(triggered()), this, SLOT(gotoBlockBrowser()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));
    connect(masternodeManagerAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(masternodeManagerAction, SIGNAL(triggered()), this, SLOT(gotoMasternodeManagerPage()));
    connect(messageAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(messageAction, SIGNAL(triggered()), this, SLOT(gotoMessagePage()));

    quitAction = new QAction(QIcon(":icons/quit"), tr("E&xit"), this);
    quitAction->setToolTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(QIcon(":/icons/bitcoin"), tr("&About DigitalNote"), this);
    aboutAction->setToolTip(tr("Show information about DigitalNote"));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutQtAction = new QAction(QIcon(":/qt-project.org/qmessagebox/images/qtlogo-64.png"), tr("About &Qt"), this);
    aboutQtAction->setToolTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(QIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setToolTip(tr("Modify configuration options for DigitalNote"));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    toggleHideAction = new QAction(QIcon(":/icons/bitcoin"), tr("&Show / Hide"), this);
    encryptWalletAction = new QAction(QIcon(":/icons/lock_closed_toolbar"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setToolTip(tr("Encrypt or decrypt wallet"));
    backupWalletAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setToolTip(tr("Backup wallet to another location"));
    importPrivateKeyAction = new QAction(QIcon(":/icons/key"), tr("&Import private key..."), this);
    importPrivateKeyAction->setToolTip(tr("Import a private key"));
    changePassphraseAction = new QAction(QIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setToolTip(tr("Change the passphrase used for wallet encryption"));
    unlockWalletAction = new QAction(QIcon(":/icons/lock_open_toolbar"),tr("&Unlock Wallet..."), this);
    unlockWalletAction->setToolTip(tr("Unlock wallet"));
    lockWalletAction = new QAction(QIcon(":/icons/lock_closed_toolbar"),tr("&Lock Wallet"), this);
    lockWalletAction->setToolTip(tr("Lock wallet"));
    unlockForStakingAction = new QAction(QIcon(":/icons/lock_open_toolbar"),tr("Unlock for &Staking..."), this);
    unlockForStakingAction->setToolTip(tr("Unlock the wallet for staking only — sends still require a full unlock"));
    signMessageAction = new QAction(QIcon(":/icons/edit"), tr("Sign &message..."), this);
    verifyMessageAction = new QAction(QIcon(":/icons/transaction_0"), tr("&Verify message..."), this);
	checkWalletAction = new QAction(QIcon(":/icons/transaction_confirmed"), tr("&Check Wallet..."), this);
	checkWalletAction->setStatusTip(tr("Check wallet integrity and report findings"));
	repairWalletAction = new QAction(QIcon(":/icons/options"), tr("&Repair Wallet..."), this);
	repairWalletAction->setStatusTip(tr("Fix wallet integrity and remove orphans"));

	compactWalletAction = new QAction(QIcon(":/icons/options"), tr("&Compact Wallet..."), this);
	compactWalletAction->setStatusTip(tr("Rebuild wallet.dat to reclaim space (restarts the wallet, takes time)"));

	lockedOutputsAction = new QAction(QIcon(":/icons/lock_closed_solid"), tr("&Locked Outputs..."), this);
	lockedOutputsAction->setStatusTip(tr("View and manage all currently locked outputs"));
	
    exportAction = new QAction(QIcon(":/icons/export"), tr("&Export..."), this);
    exportAction->setToolTip(tr("Export the data in the current tab to a file"));
    openRPCConsoleAction = new QAction(QIcon(":/icons/debugwindow"), tr("&Debug window"), this);
    openRPCConsoleAction->setToolTip(tr("Open debugging and diagnostic console"));

    editConfigAction = new QAction(QIcon(":/icons/editconf"), tr("&Edit DigitalNote.conf"), this);
    editConfigAction->setToolTip(tr("Edit the configuration file for DigitalNote"));
    editConfigExtAction = new QAction(QIcon(":/icons/editconf"), tr("&Edit DigitalNote.conf (external)"), this);
    editConfigExtAction->setToolTip(tr("Edit the configuration file for DigitalNote (external editor)"));
    openDataDirAction = new QAction(QIcon(":/icons/folder"), tr("&Open data dir"), this);
    openDataDirAction->setToolTip(tr("Open the directory where DigitalNote data is stored"));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(encryptWalletAction, SIGNAL(triggered()), this, SLOT(encryptWallet()));
    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(importPrivateKeyAction, SIGNAL(triggered()), this, SLOT(importPrivateKey()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
    connect(unlockWalletAction, SIGNAL(triggered()), this, SLOT(unlockWallet()));
    connect(lockWalletAction, SIGNAL(triggered()), this, SLOT(lockWallet()));
    connect(unlockForStakingAction, SIGNAL(triggered()), this, SLOT(unlockForStaking()));
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
	connect(checkWalletAction, SIGNAL(triggered()), this, SLOT(checkWallet()));
    connect(repairWalletAction, SIGNAL(triggered()), this, SLOT(repairWallet()));
    connect(compactWalletAction, SIGNAL(triggered()), this, SLOT(compactWallet()));
    connect(lockedOutputsAction, SIGNAL(triggered()), this, SLOT(showLockedOutputs()));
    connect(editConfigAction, SIGNAL(triggered()), this, SLOT(editConfig()));
    connect(editConfigExtAction, SIGNAL(triggered()), this, SLOT(editConfigExt()));
    connect(openDataDirAction, SIGNAL(triggered()), this, SLOT(openDataDir()));

    seedPhraseAction = new QAction(QIcon(":/icons/key"), tr("&Recovery Phrase..."), this);
    seedPhraseAction->setToolTip(
        tr("View your 24-word wallet recovery phrase.\n\n"
           "Note: Only available for wallets encrypted in DigitalNote v2.0.0.7 or later.\n"
           "Older encrypted wallets do not have a recovery phrase stored."));
    connect(seedPhraseAction, SIGNAL(triggered()), this, SLOT(showSeedPhrase()));
}

void DigitalNoteGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    appMenuBar = new QMenuBar();
#else
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    file->addAction(backupWalletAction);
    file->addAction(importPrivateKeyAction);
    file->addAction(exportAction);
    file->addAction(signMessageAction);
    file->addAction(verifyMessageAction);
    file->addSeparator();
    file->addAction(quitAction);
	
    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    settings->addAction(encryptWalletAction);
    settings->addAction(changePassphraseAction);
    settings->addAction(unlockWalletAction);
    settings->addAction(unlockForStakingAction);
    settings->addAction(lockWalletAction);
    settings->addAction(seedPhraseAction);
    settings->addSeparator();
    settings->addAction(optionsAction);

    // Tools menu: maintenance operations.  Show Backups opens the wallet
    // backup folder; Check/Repair Wallet validate and recover BDB state
    // without restart; Compact Wallet is the dump-and-restore rebuild
    // (long-running, requires restart).  All four were previously in
    // the Settings menu where they didn't belong (Settings is for
    // security state and preferences); the Tools menu was added in this
    // cycle for Compact Wallet and was a logical home for the rest.
    QMenu *tools = appMenuBar->addMenu(tr("&Tools"));
    tools->addAction(showBackupsAction);
    tools->addSeparator();
    tools->addAction(checkWalletAction);
    tools->addAction(repairWalletAction);
    tools->addAction(compactWalletAction);
    tools->addSeparator();
    tools->addAction(lockedOutputsAction);
    tools->addSeparator();
    tools->addAction(openRPCConsoleAction);
    tools->addAction(openDataDirAction);
    tools->addAction(editConfigAction);
    tools->addAction(editConfigExtAction);

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

static QWidget* makeToolBarSpacer()
{
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    spacer->setStyleSheet("QWidget { background: #121418; }");
    if(!fUseDarkTheme)
    {
        spacer->setStyleSheet("QWidget { background: #614eb0; }");
    }
    return spacer;
}

void DigitalNoteGUI::createToolBars()
{
    fLiteMode = GetBoolArg("-litemode", false);

    toolbar = new QToolBar(tr("Tabs toolbar"));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    toolbar->setObjectName("tabs");
    toolbar->setStyleSheet("QToolBar { spacing: 0px; } QWidget { background:#121418; } QToolButton { color: #d4d4d4; font-weight:bold; background-color: #121418;} QToolButton:hover { background-color: #2f1d4b; } QToolButton:checked { background-color: #2f1d4b } QToolButton:pressed { background-color: #2f1d4b; } #tabs { color: #d4d4d4; background-color: #121418; }");
    toolbar->setIconSize(QSize(24,24));

    if(!fUseDarkTheme)
    {
        toolbar->setStyleSheet("QToolBar { spacing: 0px; } QWidget { background:#614eb0; } QToolButton { color: #ffffff; font-weight:bold; } QToolButton:hover { background-color: #3098c6; } QToolButton:checked { background-color: #3bb2e7; } QToolButton:pressed { background-color: #25779c; } #tabs { color: #ffffff; background-color: #614eb0; }");
    }

    QLabel* header = new QLabel();
    header->setMinimumSize(142, 142);
    header->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    header->setPixmap(QPixmap(":/images/header"));
    header->setMaximumSize(142,142);
    header->setScaledContents(true);
    toolbar->addWidget(header);

    toolbar->addAction(overviewAction);
    toolbar->addAction(receiveCoinsAction);
    toolbar->addAction(sendCoinsAction);
    toolbar->addAction(historyAction);
    toolbar->addAction(addressBookAction);
    toolbar->addAction(masternodeManagerAction);
    if (!fLiteMode){
        toolbar->addAction(messageAction);
    }
    toolbar->addAction(blockAction);
    netLabel = new QLabel();

    QWidget *spacer = makeToolBarSpacer();
    netLabel->setObjectName("netLabel");
    netLabel->setStyleSheet("#netLabel { color: #ffffff; }");
    toolbar->addWidget(spacer);
    toolbar->setOrientation(Qt::Vertical);
    toolbar->setMovable(false);

    addToolBar(Qt::LeftToolBarArea, toolbar);

    foreach(QAction *action, toolbar->actions()) {
        toolbar->widgetForAction(action)->setFixedWidth(142);
    }
}

void DigitalNoteGUI::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel)
    {
        // Set network label first so testnet check can override mainnet default.
        // Previously this was unconditional "MAINNET" -- testnet builds showed
        // the wrong label in the status bar.
        if(!fOnlyTor)
        {
            if(clientModel->isTestNet())
            {
                netLabel->setText("TESTNET");
                netLabel->setToolTip(tr("Connected to the XDN Testnet"));
            }
            else
            {
                netLabel->setText("MAINNET");
                netLabel->setToolTip(tr("Connected to the XDN Mainnet"));
            }
        }
        else
        {
            if(!IsLimited(NET_TOR))
            {
                netLabel->setText("TOR");
            }
        }

        // Replace some strings and icons, when using the testnet
        if(clientModel->isTestNet())
        {
            setWindowTitle(windowTitle() + QString(" ") + tr("[testnet]"));
#ifndef Q_OS_MAC
            qApp->setWindowIcon(QIcon(fUseDarkTheme ? ":/icons/dark/bitcoin-dark_testnet" : ":/icons/bitcoin_testnet"));
            setWindowIcon(QIcon(fUseDarkTheme ? ":/icons/dark/bitcoin-dark_testnet" : ":/icons/bitcoin_testnet"));
#else
            MacDockIconHandler::instance()->setIcon(QIcon(":icons/bitcoin_testnet"));
#endif
            if(trayIcon)
            {
                trayIcon->setToolTip(tr("DigitalNote client") + QString(" ") + tr("[testnet]"));
                trayIcon->setIcon(QIcon(":/icons/toolbar_testnet"));
                toggleHideAction->setIcon(QIcon(":/icons/toolbar_testnet"));
            }
        }

        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(clientModel->getNumBlocks());
        connect(clientModel, SIGNAL(numBlocksChanged(int)), this, SLOT(setNumBlocks(int)));

        // Receive and report messages from network/worker thread
        connect(clientModel, SIGNAL(message(QString,QString,bool,unsigned int)), this, SLOT(message(QString,QString,bool,unsigned int)));

        // Show progress dialog
        connect(clientModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));
        // NOTE: walletModel showProgress connect moved to setWalletModel().
        // walletModel is not yet set when setClientModel runs, and the
        // null-pointer connect logged a Qt warning at startup.

        overviewPage->setClientModel(clientModel);
        rpcConsole->setClientModel(clientModel);
        addressBookPage->setOptionsModel(clientModel->getOptionsModel());
        receiveCoinsPage->setOptionsModel(clientModel->getOptionsModel());

        // If a previous launch attempted a Compact Wallet rebuild, surface
        // the outcome to the user via a one-shot dialog. Deferred via
        // singleShot so it fires once the window is shown (otherwise the
        // dialog appears before the main window does, which looks broken).
        QTimer::singleShot(0, this, SLOT(showRebuildResultIfPresent()));
    }
}

void DigitalNoteGUI::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if(walletModel)
    {
        // Receive and report messages from wallet thread
        connect(walletModel, SIGNAL(message(QString,QString,bool,unsigned int)), this, SLOT(message(QString,QString,bool,unsigned int)));
        connect(sendCoinsPage, SIGNAL(message(QString,QString,bool,unsigned int)), this, SLOT(message(QString,QString,bool,unsigned int)));

        // Put transaction list in tabs
        transactionView->setModel(walletModel);
        overviewPage->setWalletModel(walletModel);
        addressBookPage->setModel(walletModel->getAddressTableModel());
        receiveCoinsPage->setModel(walletModel->getAddressTableModel());
        sendCoinsPage->setModel(walletModel);
        signVerifyMessageDialog->setModel(walletModel);
        blockBrowser->setModel(walletModel);
        masternodeManagerPage->setWalletModel(walletModel);

        setEncryptionStatus(walletModel->getEncryptionStatus());
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));
                connect(walletModel, SIGNAL(recoveryPhraseUpgradeAvailable()),
                this, SLOT(onRecoveryPhraseUpgradeAvailable()));
        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingTransaction(QModelIndex,int,int)));

        // B1: prompt to lock fresh masternode-collateral-shaped UTXOs
        connect(walletModel, SIGNAL(collateralCandidateReceived(QString,int)),
                this, SLOT(onCollateralCandidateReceived(QString,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));

        // Show progress dialog (moved from setClientModel where walletModel
        // was still null at the time of the connect).
        connect(walletModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));
    }
}

void DigitalNoteGUI::setMessageModel(MessageModel *messageModel)
{
    this->messageModel = messageModel;
    if(messageModel)
    {
        // Report errors from message thread
        connect(messageModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        // Put transaction list in tabs
        messagePage->setModel(messageModel);

        // Balloon pop-up for new message
        connect(messageModel, SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingMessage(QModelIndex,int,int)));
    }
}

void DigitalNoteGUI::createTrayIcon()
{
    QMenu *trayIconMenu;
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip(tr("DigitalNote client"));
    trayIcon->setIcon(QIcon(":/icons/toolbar"));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    trayIcon->show();
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(receiveCoinsAction);
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
    trayIconMenu->addAction(showBackupsAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif

    notificator = new Notificator(qApp->applicationName(), trayIcon);
}

#ifndef Q_OS_MAC
void DigitalNoteGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHideAction->trigger();
    }
}
#endif

void DigitalNoteGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;
    OptionsDialog dlg;
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void DigitalNoteGUI::aboutClicked()
{
    AboutDialog dlg;
    dlg.setModel(clientModel);
    dlg.exec();
}

void DigitalNoteGUI::setNumConnections(int count)
{
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }
    labelConnectionsIcon->setPixmap(QIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    labelConnectionsIcon->setToolTip(tr("%n active connection(s) to DigitalNote network", "", count));
}

void DigitalNoteGUI::setNumBlocks(int count)
{
    QString tooltip;

    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    QDateTime currentDate = QDateTime::currentDateTime();
    int totalSecs = GetTime() - Params().GenesisBlock().GetBlockTime();
    int secs = lastBlockDate.secsTo(currentDate);

    tooltip = tr("Processed %1 blocks of transaction history.").arg(count);

    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60)
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

        overviewPage->showOutOfSyncWarning(false);

        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);

        // Update network label tooltip with current block height.
        // v2.0.0.8 PB-12: branch on testnet vs mainnet to match the
        // initial netLabel tooltip set at startup (~line 585-594).
        // Previously this was unconditional "Mainnet" -- testnet builds
        // showed the wrong label here even though the rest of the UI
        // correctly identified itself as testnet.
        if (netLabel)
        {
            if (clientModel && clientModel->isTestNet())
            {
                netLabel->setToolTip(tr("Synced to the XDN Testnet (Block Height: %1)")
                    .arg(count));
            }
            else
            {
                netLabel->setToolTip(tr("Synced to the XDN Mainnet (Block Height: %1)")
                    .arg(count));
            }
        }

        // A9: catchup just finished -- emit summary toast for any
        // transactions whose individual toasts were suppressed during
        // catchup.  No-op if we weren't in a batch.
        maybeEmitBatchSummary();
    }
    else
    {
        // Represent time from last generated block in human readable text
        QString timeBehindText;
        const int HOUR_IN_SECONDS = 60*60;
        const int DAY_IN_SECONDS = 24*60*60;
        const int WEEK_IN_SECONDS = 7*24*60*60;
        const int YEAR_IN_SECONDS = 31556952; // Average length of year in Gregorian calendar
        if(secs < 2*DAY_IN_SECONDS)
        {
            timeBehindText = tr("%n hour(s)","",secs/HOUR_IN_SECONDS);
        }
        else if(secs < 2*WEEK_IN_SECONDS)
        {
            timeBehindText = tr("%n day(s)","",secs/DAY_IN_SECONDS);
        }
        else if(secs < YEAR_IN_SECONDS)
        {
            timeBehindText = tr("%n week(s)","",secs/WEEK_IN_SECONDS);
        }
        else
        {
            int years = secs / YEAR_IN_SECONDS;
            int remainder = secs % YEAR_IN_SECONDS;
            timeBehindText = tr("%1 and %2").arg(tr("%n year(s)", "", years)).arg(tr("%n week(s)","", remainder/WEEK_IN_SECONDS));
        }

        progressBarLabel->setText(tr(clientModel->isImporting() ? "Importing blocks..." : "Synchronizing with network..."));
        progressBarLabel->setVisible(true);
        progressBarLabel->setStyleSheet("QLabel { color: #ffffff; }");
        progressBar->setFormat(tr("%1 behind").arg(timeBehindText));
        progressBar->setMaximum(totalSecs);
        progressBar->setValue(totalSecs - secs);
        progressBar->setVisible(true);

        // Update MAINNET label tooltip with real peer chain height
        if (netLabel) {
            int peerHeight = clientModel->getNumBlocksOfPeers();
            netLabel->setToolTip(tr("Syncing block %1 of %2")
                .arg(count)
                .arg(peerHeight));
        }

        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        labelBlocksIcon->setMovie(syncIconMovie);
        if(count != prevBlocks)
            syncIconMovie->jumpToNextFrame();
        prevBlocks = count;

        overviewPage->showOutOfSyncWarning(true);

        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1 ago.").arg(timeBehindText);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);

    statusBar()->setVisible(true);
}

void DigitalNoteGUI::message(const QString &title, const QString &message, bool modal, unsigned int style)
{
    QString strTitle = tr("DigitalNote") + " - ";
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    // Check for usage of predefined title
    switch (style) {
    case CClientUIInterface::MSG_ERROR:
        strTitle += tr("Error");
        break;
    case CClientUIInterface::MSG_WARNING:
        strTitle += tr("Warning");
        break;
    case CClientUIInterface::MSG_INFORMATION:
        strTitle += tr("Information");
        break;
    default:
        strTitle += title; // Use supplied title
    }

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    }
    else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    // Display message
    if (modal) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
            buttons = QMessageBox::Ok;

        QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, message, buttons);
        // Defensive: ensure the message box rises above the splash and any
        // other top-stay window.  bitcoin.cpp's ThreadSafeMessageBox also
        // hides the splash before reaching us, but if some other path
        // bypasses that, this self-raise still gets the dialog visible.
        mBox.setWindowFlags(mBox.windowFlags() | Qt::WindowStaysOnTopHint);
        mBox.show();
        mBox.raise();
        mBox.activateWindow();
        mBox.exec();
    }
    else
        notificator->notify((Notificator::Class)nNotifyIcon, strTitle, message);
}

void DigitalNoteGUI::error(const QString &title, const QString &message, bool modal)
{
    // Report errors from network/worker thread
    if(modal)
    {
        QMessageBox::critical(this, title, message, QMessageBox::Ok, QMessageBox::Ok);
    } else {
        notificator->notify(Notificator::Critical, title, message);
    }
}

void DigitalNoteGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void DigitalNoteGUI::closeEvent(QCloseEvent *event)
{
    if(clientModel)
    {
#ifndef Q_OS_MAC // Ignored on Mac
        if(!clientModel->getOptionsModel()->getMinimizeToTray() &&
           !clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            // close rpcConsole in case it was open to make some space for the shutdown window
            rpcConsole->close();

            qApp->quit();
        }
#endif
    }
    QMainWindow::closeEvent(event);
}

void DigitalNoteGUI::askFee(qint64 nFeeRequired, bool *payFee)
{
    if (!clientModel || !clientModel->getOptionsModel())
        return;

    QString strMessage = tr("This transaction is over the size limit. You can still send it for a fee of %1, "
        "which goes to the nodes that process your transaction and helps to support the network. "
        "Do you want to pay the fee?").arg(DigitalNoteUnits::formatWithUnit(clientModel->getOptionsModel()->getDisplayUnit(), nFeeRequired));
    QMessageBox::StandardButton retval = QMessageBox::question(
          this, tr("Confirm transaction fee"), strMessage,
          QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Yes);
    *payFee = (retval == QMessageBox::Yes);
}

void DigitalNoteGUI::incomingTransaction(const QModelIndex & parent, int start, int end)
{
	// Prevent balloon-spam when initial block download is in progress
    if(!walletModel || !clientModel)
        return;

    // A9: if we're in a batch (IBD/catchup or explicit
    // rescan/import), count the tx and suppress the per-tx toast.
    // The summary toast fires from maybeEmitBatchSummary() when the
    // batch ends.  The kind is set the first time we see a tx in
    // this batch and is preserved until the batch ends so the
    // summary text can name the right activity.
    bool inIBD = clientModel->inInitialBlockDownload();
    bool inExplicit = walletModel->processingQueuedTransactions();
    if (inIBD || inExplicit)
    {
        if (!fInBatchMode)
        {
            fInBatchMode = true;
            eBatchKind = inExplicit ? BATCH_IMPORT : BATCH_SYNC;
        }
        ++nBatchTxCount;
        return;
    }

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();

    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent)
                    .data(Qt::EditRole).toULongLong();
    QString date = ttm->index(start, TransactionTableModel::Date, parent)
                    .data().toString();
    QString type = ttm->index(start, TransactionTableModel::Type, parent)
                    .data().toString();
    QString address = ttm->index(start, TransactionTableModel::ToAddress, parent)
                    .data().toString();
    QIcon icon = qvariant_cast<QIcon>(ttm->index(start,
                        TransactionTableModel::ToAddress, parent)
                    .data(Qt::DecorationRole));
    // On new transaction, make an info balloon
    notificator->notify(Notificator::Information,
                        (amount)<0 ? tr("Sent transaction") :
                                     tr("Incoming transaction"),
                          tr("Date: %1\n"
                             "Amount: %2\n"
                             "Type: %3\n"
                             "Address: %4\n")
                          .arg(date)
                          .arg(DigitalNoteUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), amount, true))
                          .arg(type)
                          .arg(address), icon);
}

// B1: prompt for fresh masternode collateral.  Fired from
// TransactionTablePriv via WalletModel after a CT_NEW tx that has a
// 2,000,000-XDN spendable, unlocked vout.  Per-UTXO suppression is
// implicit (CT_NEW only fires once per UTXO), but the user can opt out
// of all future prompts FOR THIS WALLET via the third button.
void DigitalNoteGUI::onCollateralCandidateReceived(const QString &txidHex,
                                                   int vout)
{
    if (!walletModel || !pwalletMain)
        return;

    // Honour per-wallet suppression: stored in QSettings, keyed by the
    // hashed absolute wallet path (see GuiState).  No prompt if the
    // user has previously opted out for this wallet.
    const std::string walletPath =
        (GetDataDir() / boost::filesystem::path(pwalletMain->strWalletFile)).string();
    if (GuiState::is2MCollateralPromptSuppressed(walletPath))
        return;

    // Defensive re-check: the candidate was qualified inside
    // TransactionTablePriv under the wallet locks.  We're now on the
    // GUI thread and the state may have moved on (e.g. a stake spent
    // it, the user manually locked it).  Re-verify before prompting.
    uint256 hash;
    hash.SetHex(txidHex.toStdString());
    if (walletModel->isLockedCoin(hash, static_cast<unsigned int>(vout)))
        return;

    QMessageBox box(this);
    box.setWindowTitle(tr("Masternode collateral received"));
    box.setIcon(QMessageBox::Question);

    const QString amountStr =
        DigitalNoteUnits::formatWithUnit(
            walletModel->getOptionsModel()->getDisplayUnit(),
            static_cast<qint64>(MasternodeCollateral(nBestHeight)) * COIN,
            true);

    box.setText(tr(
        "<p>You have received <b>%1</b> &mdash; the masternode "
        "collateral amount.</p>"
        "<p>Lock this UTXO to prevent it from being spent "
        "accidentally (e.g. as the input to a stake or a regular "
        "send)?</p>"
        "<p style=\"color:#888;font-size:90%;\">UTXO: %2:%3</p>"
    ).arg(amountStr).arg(txidHex).arg(vout));

    QPushButton *lockBtn = box.addButton(
        tr("Lock as collateral"), QMessageBox::AcceptRole);
    QPushButton *laterBtn = box.addButton(
        tr("Not now"), QMessageBox::RejectRole);
    QPushButton *neverBtn = box.addButton(
        tr("Don't ask for this wallet"), QMessageBox::DestructiveRole);
    neverBtn->setStyleSheet("QPushButton { color:#888; }");

    box.setDefaultButton(lockBtn);
    box.exec();

    QAbstractButton *clicked = box.clickedButton();
    if (clicked == lockBtn)
    {
        COutPoint out(hash, static_cast<unsigned int>(vout));
        walletModel->lockCoin(out);
    }
    else if (clicked == neverBtn)
    {
        GuiState::set2MCollateralPromptSuppressed(walletPath);
    }
    // laterBtn (or window-close): no action -- user will be prompted
    // again if another fresh 2M UTXO arrives.
}

// A9: Called from the spots that detect a batch ending --
// setNumBlocks() (when sync catches up to wallet's "current" threshold)
// and showProgress() (when an explicit rescan/import finishes).
// Emits one summary toast for the batch, resets state, idempotent if
// no batch was active.
void DigitalNoteGUI::maybeEmitBatchSummary()
{
    if (!fInBatchMode)
        return;

    if (notificator && nBatchTxCount > 0)
    {
        QString title;
        QString text;
        switch (eBatchKind)
        {
        case BATCH_IMPORT:
            title = tr("Import complete");
            text = tr("%n transaction(s) added to wallet during import", "", nBatchTxCount);
            break;
        case BATCH_SYNC:
        default:
            title = tr("Sync complete");
            text = tr("%n transaction(s) added to wallet during chain catchup", "", nBatchTxCount);
            break;
        }
        notificator->notify(Notificator::Information, title, text);
    }

    nBatchTxCount = 0;
    fInBatchMode = false;
    eBatchKind = BATCH_NONE;
}

void DigitalNoteGUI::incomingMessage(const QModelIndex & parent, int start, int end)
{
    if(!messageModel)
        return;

    MessageModel *mm = messageModel;

    if (mm->index(start, MessageModel::TypeInt, parent).data().toInt() == MessageTableEntry::Received)
    {
        QString sent_datetime = mm->index(start, MessageModel::ReceivedDateTime, parent).data().toString();
        QString from_address  = mm->index(start, MessageModel::FromAddress,      parent).data().toString();
        QString to_address    = mm->index(start, MessageModel::ToAddress,        parent).data().toString();
        QString message       = mm->index(start, MessageModel::Message,          parent).data().toString();
        QTextDocument html;
        html.setHtml(message);
        QString messageText(html.toPlainText());
        notificator->notify(Notificator::Information,
                            tr("Incoming Message"),
                            tr("Date: %1\n"
                               "From Address: %2\n"
                               "To Address: %3\n"
                               "Message: %4\n")
                              .arg(sent_datetime)
                              .arg(from_address)
                              .arg(to_address)
                              .arg(messageText));
    };
}

void DigitalNoteGUI::clearWidgets()
{
    centralStackedWidget->setCurrentWidget(centralStackedWidget->widget(0));
    for(int i = centralStackedWidget->count(); i>0; i--){
        QWidget* widget = centralStackedWidget->widget(i);
        centralStackedWidget->removeWidget(widget);
        widget->deleteLater();
    }
}

void DigitalNoteGUI::gotoMasternodeManagerPage()
{
    masternodeManagerAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(masternodeManagerPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void DigitalNoteGUI::gotoBlockBrowser()
{
    blockAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(blockBrowser);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void DigitalNoteGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(overviewPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void DigitalNoteGUI::gotoHistoryPage()
{
    if(!fGUIunlock) {
        QMessageBox::information(this, tr("Wallet is locked"),
                             tr("Please unlock your wallet to use this feature."));
        return;
    }

    historyAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(transactionsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));
}

void DigitalNoteGUI::gotoAddressBookPage()
{
    if(!fGUIunlock) {
        QMessageBox::information(this, tr("Wallet is locked"),
                             tr("Please unlock your wallet to use this feature."));
        return;
    }

    addressBookAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(addressBookPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), addressBookPage, SLOT(exportClicked()));
}

void DigitalNoteGUI::gotoReceiveCoinsPage()
{
    if(!fGUIunlock) {
        QMessageBox::information(this, tr("Wallet is locked"),
                             tr("Please unlock your wallet to use this feature."));
        return;
    }

    receiveCoinsAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(receiveCoinsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), receiveCoinsPage, SLOT(exportClicked()));
}

void DigitalNoteGUI::gotoSendCoinsPage()
{
    if(!fGUIunlock) {
        QMessageBox::information(this, tr("Wallet is locked"),
                             tr("Please unlock your wallet to use this feature."));
        return;
    }

    sendCoinsAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(sendCoinsPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void DigitalNoteGUI::gotoSignMessageTab(QString addr)
{
    if(!fGUIunlock) {
        QMessageBox::information(this, tr("Wallet is locked"),
                             tr("Please unlock your wallet to use this feature."));
        return;
    }

    // call show() in showTab_SM()
    signVerifyMessageDialog->showTab_SM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void DigitalNoteGUI::gotoVerifyMessageTab(QString addr)
{
    if(!fGUIunlock) {
        QMessageBox::information(this, tr("Wallet is locked"),
                             tr("Please unlock your wallet to use this feature."));
        return;
    }

    // call show() in showTab_VM()
    signVerifyMessageDialog->showTab_VM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

void DigitalNoteGUI::gotoMessagePage()
{
    if(!fGUIunlock) {
        QMessageBox::information(this, tr("Wallet is locked"),
                             tr("Please unlock your wallet to use this feature."));
        return;
    }

    messageAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(messagePage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), messagePage, SLOT(exportClicked()));
}

void DigitalNoteGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void DigitalNoteGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        int nValidUrisFound = 0;
        QList<QUrl> uris = event->mimeData()->urls();
        foreach(const QUrl &uri, uris)
        {
            if (sendCoinsPage->handleURI(uri.toString()))
                nValidUrisFound++;
        }

        // if valid URIs were found
        if (nValidUrisFound)
            gotoSendCoinsPage();
        else
            notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid DigitalNote address or malformed URI parameters."));
    }

    event->acceptProposedAction();
}

void DigitalNoteGUI::handleURI(QString strURI)
{
    // URI has to be valid
    if (sendCoinsPage->handleURI(strURI))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
    }
    else
        notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid DigitalNote address or malformed URI parameters."));
}

void DigitalNoteGUI::setEncryptionStatus(int status)
{
    if(fWalletUnlockStakingOnly)
    {
    labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked for staking only</b>"));
        changePassphraseAction->setEnabled(false);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(true);
        unlockForStakingAction->setVisible(false);  // already in this state
        encryptWalletAction->setEnabled(false);
        fGUIunlock = false;
    }
    else
    {

    switch(status)
    {
    case WalletModel::Unencrypted:
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>not encrypted</b>"));
        changePassphraseAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(false);
        unlockForStakingAction->setVisible(false);  // not encrypted, no point
        encryptWalletAction->setEnabled(true);
        encryptWalletAction->setText(tr("&Encrypt Wallet..."));
        fGUIunlock = true;
        break;
    case WalletModel::Unlocked:
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(true);
        unlockForStakingAction->setVisible(false);  // already fully unlocked
        encryptWalletAction->setEnabled(false);
        encryptWalletAction->setText(tr("&Encrypt Wallet..."));
        fGUIunlock = true;
        break;
    case WalletModel::Locked:
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(false);
        unlockForStakingAction->setVisible(true);   // primary use case
        encryptWalletAction->setEnabled(false);
        encryptWalletAction->setText(tr("&Decrypt Wallet..."));
        encryptWalletAction->setToolTip(tr("Unlock your wallet first, then use Decrypt Wallet."));
        fGUIunlock = false;
        break;
    }

    }
}

void DigitalNoteGUI::onRecoveryPhraseUpgradeAvailable()
{
    LogPrintf("BitcoinGUI: onRecoveryPhraseUpgradeAvailable slot fired\n");
 
    if (!walletModel) {
        LogPrintf("BitcoinGUI: bail - no walletModel\n");
        return;
    }
 
    if (!walletModel->needsRecoveryPhraseUpgrade()) {
        LogPrintf("BitcoinGUI: bail - needsRecoveryPhraseUpgrade returned false\n");
        return;
    }
 
    LogPrintf("BitcoinGUI: opening RecoveryPhraseUpgradeDialog\n");
    RecoveryPhraseUpgradeDialog dlg(walletModel, this);
    dlg.exec();
    LogPrintf("BitcoinGUI: RecoveryPhraseUpgradeDialog closed\n");
}

void DigitalNoteGUI::encryptWallet()
{
    if(!walletModel)
        return;

    AskPassphraseDialog dlg(AskPassphraseDialog::Encrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    setEncryptionStatus(walletModel->getEncryptionStatus());
}

void DigitalNoteGUI::checkWallet()
{
	int nMismatchSpent;
	int64_t nBalanceInQuestion;

	if(!walletModel)
	{
		return;
	}
	
	// Check the wallet as requested by user
	walletModel->checkWallet(nMismatchSpent, nBalanceInQuestion);

	if (nMismatchSpent == 0)
	{
		notificator->notify(
			Notificator::Warning,
			tr("Check Wallet Information"),
			tr(
				"Wallet passed integrity test!\n"
				"Nothing found to fix."
			)
		);
	}
	else
	{
		notificator->notify(
			Notificator::Warning, //tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid MotaCoin address or malformed URI parameters."));
			tr("Check Wallet Information"),
			tr(
				"Wallet failed integrity test!\n\n"
				"Mismatched coin(s) found: %1.\n"
				"Amount in question: %2.\n"
				"Orphans found: %3.\n\n"
				"Please backup wallet and run repair wallet.\n"
			)
			.arg(nMismatchSpent)
			.arg(DigitalNoteUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
		);
	}
}

void DigitalNoteGUI::repairWallet()
{
    int nMismatchSpent;
    int64_t nBalanceInQuestion;
	
    if(!walletModel)
        return;

    // Repair the wallet as requested by user
    walletModel->repairWallet(nMismatchSpent, nBalanceInQuestion);

	if (nMismatchSpent == 0)
	{
		notificator->notify(Notificator::Warning,
			tr("Repair Wallet Information"),
			tr(
				"Wallet passed integrity test!\n"
				"Nothing found to fix."
			)
		);
	}
	else
	{
		notificator->notify(Notificator::Warning,
			tr("Repair Wallet Information"),
			tr(
				"Wallet failed integrity test and has been repaired!\n"
				"Mismatched coin(s) found: %1\n"
				"Amount affected by repair: %2\n"
			).arg(nMismatchSpent)
			.arg(DigitalNoteUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
		);
	}
}

void DigitalNoteGUI::compactWallet()
{
    // Q2 confirmation: explain the ramifications, not just the action.
    // Hours-long, wallet unusable, restart required, original preserved.
    // The user must understand all of this -- a plain "OK" button is
    // not enough warning for an operation of this magnitude.
    QMessageBox box(this);
    box.setWindowTitle(tr("Compact Wallet"));
    box.setIcon(QMessageBox::Warning);
    box.setText(tr(
        "<p><b>Compact Wallet rebuilds your wallet.dat file.</b></p>"
        "<p>This is a maintenance operation that reclaims free pages "
        "and rebuilds the wallet's internal structure, often producing "
        "a smaller and faster wallet file. It is a safe operation: your "
        "private keys, addresses, balances, and transactions are all "
        "preserved.</p>"
        "<p><b>Before you continue, you should understand:</b></p>"
        "<ul>"
        "<li>The wallet will <b>shut down and restart</b> automatically.</li>"
        "<li>The rebuild can take <b>minutes to hours</b> on a large "
        "wallet. The wallet is unusable while it runs.</li>"
        "<li>Your original wallet will be preserved as "
        "<b>wallet.dat.bak</b> in your data directory. If anything goes "
        "wrong you can restore it manually.</li>"
        "<li>A rescan will run after the rebuild to refresh the "
        "transaction cache.</li>"
        "</ul>"
        "<p>It is strongly recommended that you take an independent "
        "backup of <b>wallet.dat</b> before continuing.</p>"
        "<p>Proceed with the rebuild?</p>"));

    QPushButton *proceed = box.addButton(tr("Rebuild and restart"),
                                         QMessageBox::AcceptRole);
    QPushButton *cancel  = box.addButton(tr("Cancel"),
                                         QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();

    if (box.clickedButton() != proceed)
    {
        LogPrintf("CompactWallet: user cancelled.\n");
        return;
    }

    // Write the pending flag so init.cpp picks up the rebuild request on
    // next launch. The actual rebuild runs after restart, before LoadWallet.
    if (!RebuildPendingFlagWrite())
    {
        QMessageBox::critical(this, tr("Compact Wallet"),
            tr("Could not write the rebuild request flag to your data "
               "directory. Check filesystem permissions and try again."));
        return;
    }

    LogPrintf("CompactWallet: pending flag written; requesting shutdown.\n");

    // Tell the user what to expect AFTER they click OK on this dialog.
    // The shutdown will close the wallet immediately; we want the user
    // prepared for that.
    QMessageBox::information(this, tr("Compact Wallet"),
        tr("The wallet will now shut down. Restart it to begin the "
           "rebuild. You will see a maintenance-mode splash screen "
           "during the rebuild."));

    // QApplication::quit() drives Qt's normal shutdown sequence, which
    // calls into init.cpp's Shutdown() and closes BDB cleanly. The next
    // invocation of the wallet will detect the pending flag and run
    // RebuildWallet before LoadWallet.
    QApplication::quit();
}

void DigitalNoteGUI::showRebuildResultIfPresent()
{
    std::string reason;
    RebuildResultState state = RebuildResultRead(reason);
    if (state == REBUILD_RESULT_NONE)
    {
        return;
    }

    QString title = tr("Compact Wallet");
    QString msg;
    QMessageBox::Icon icon = QMessageBox::Information;

    switch (state)
    {
    case REBUILD_RESULT_SUCCESS:
        icon = QMessageBox::Information;
        msg = tr("Your wallet was rebuilt successfully. The original "
                 "wallet has been preserved as <b>wallet.dat.bak</b> in "
                 "your data directory; you can delete it once you have "
                 "confirmed the rebuilt wallet works correctly.");
        break;
    case REBUILD_RESULT_RECOVERED_FROM_CRASH:
        icon = QMessageBox::Warning;
        msg = tr("A previous Compact Wallet operation was interrupted "
                 "before it could finish. The rebuild has now been "
                 "completed automatically and your previous wallet is "
                 "preserved as <b>wallet.dat.bak</b>.");
        break;
    case REBUILD_RESULT_FAILED_PRESWAP:
        icon = QMessageBox::Critical;
        msg = tr("Compact Wallet failed before any changes were made to "
                 "your wallet file. Your wallet is exactly as it was "
                 "before. See the debug log for details.");
        break;
    case REBUILD_RESULT_FAILED_FILESYSTEM:
        icon = QMessageBox::Critical;
        msg = tr("Compact Wallet failed during a filesystem operation. "
                 "Your original wallet has been restored. See the debug "
                 "log for details.");
        break;
    default:
        // REBUILD_RESULT_NONE handled by early return above.
        return;
    }

    if (!reason.empty())
    {
        msg += "<br><br><i>" + QString::fromStdString(reason) + "</i>";
    }

    QMessageBox box(this);
    box.setWindowTitle(title);
    box.setIcon(icon);
    box.setText(msg);
    box.exec();

    // Consume the marker so this dialog never re-fires. If removal
    // fails (filesystem permission issue), log it but don't bother the
    // user further -- the worst case is they see the dialog again next
    // launch, which is a mild annoyance not a correctness problem.
    if (!RebuildResultRemove())
    {
        LogPrintf("CompactWallet: failed to remove result marker; "
                  "the dialog may re-fire on next launch.\n");
    }
}

void DigitalNoteGUI::showLockedOutputs()
{
    // Modal dialog -- stack-allocated, scoped to the function call.
    // Refreshes itself on showEvent and on every toggle, so we don't
    // need to call refresh() explicitly here.  setWalletModel is what
    // hands the dialog its data source; without it the dialog shows
    // an empty table and a "No wallet." status.
    LockedOutputsDialog dlg(this);
    dlg.setWalletModel(walletModel);
    dlg.exec();
}

void DigitalNoteGUI::backupWallet()
{
    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString filename = QFileDialog::getSaveFileName(this, tr("Backup Wallet"), saveDir, tr("Wallet Data (*.dat)"));
    if(!filename.isEmpty()) {
        if(!walletModel->backupWallet(filename)) {
            QMessageBox::warning(this, tr("Backup Failed"), tr("There was an error trying to save the wallet data to the new location."));
        }
    }
}

void DigitalNoteGUI::importPrivateKey()
{
    ImportPrivateKeyDialog dlg(this);
    dlg.setModel(walletModel->getAddressTableModel());
    dlg.exec();
}

void DigitalNoteGUI::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void DigitalNoteGUI::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        // Always use Unlock mode — staking checkbox is available but unticked by default
        AskPassphraseDialog::Mode mode = AskPassphraseDialog::Unlock;
        AskPassphraseDialog dlg(mode, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void DigitalNoteGUI::lockWallet()
{
    if(!walletModel)
        return;

    walletModel->setWalletLocked(true);
}

void DigitalNoteGUI::unlockForStaking()
{
    if(!walletModel)
        return;

    // Only meaningful when the wallet is encrypted but not already
    // unlocked-for-staking-only.  setEncryptionStatus already hides
    // the menu item in the cases where this isn't applicable, but
    // we double-check here in case the slot is reached some other way.
    if(walletModel->getEncryptionStatus() == WalletModel::Unencrypted)
        return;

    AskPassphraseDialog dlg(AskPassphraseDialog::UnlockStaking, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void DigitalNoteGUI::showNormalIfMinimized(bool fToggleHidden)
{
    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void DigitalNoteGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void DigitalNoteGUI::updateWeight()
{
    if (!pwalletMain)
        return;

    // Do not poll balance until wallet load + ReacceptWalletTransactions
    // have completed. Polling earlier walks a partially-populated wallet
    // and writes wrong values (zero) to nAvailableCreditCached for any
    // wtx whose key is not yet in the keystore. Those wrong cache values
    // persist for the entire session because nothing later invalidates
    // them. Manifests as visible ~10x-low balance until restart-after-fix.
    if (!fWalletLoadComplete)
        return;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
        return;

    TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
    if (!lockWallet)
        return;

    nWeight = pwalletMain->GetStakeWeight();
}

// ===========================================================================
// v2.0.0.8 CW2: staking-icon state machine.
//
// See v208-staking-icon-state-machine-SPEC.md for the design rationale.
//
// The legacy implementation read `nLastCoinStakeSearchInterval` -- a counter
// that's only updated when SignBlock EXITS its kernel search WITHOUT finding
// a block.  On a wallet that finds blocks successfully the counter stays at
// 0, so the legacy code displayed the "warming up" clock indefinitely even
// while blocks were being produced.
//
// The new state machine reads two atomics published by ThreadStakeMiner
// (heartbeat + productivity flag) and uses a hammer-latch with a 5-minute
// safety floor so the icon doesn't flutter on transient §29 defers.  The
// expected-time-between-blocks tooltip is recomputed from nWeight +
// difficulty rather than the broken counter.
// ===========================================================================

QString DigitalNoteGUI::ComputeHammerTooltip() const
{
    // Expected time between PoS blocks for THIS wallet:
    //
    //   T = GetTargetSpacing * networkWeight / walletWeight
    //
    // Intuition: your wallet holds (walletWeight / networkWeight) of the
    // total stake; the network produces a block every GetTargetSpacing
    // seconds; so on average you find one block per
    // (networkWeight / walletWeight) blocks the network produces, i.e.
    // every GetTargetSpacing * (networkWeight / walletWeight) seconds.
    // Stochastic in practice; user-meaningful as an order-of-magnitude
    // estimate.
    //
    // This is the same formula the legacy pre-CW2 staking-icon body used.
    // GetPoSKernelPS() returns the network's total stakeable weight
    // (despite the "PS" naming convention -- this codebase exposes it as
    // "netstakeweight" in the getstakinginfo RPC; see rpcmining.cpp:151
    // for the canonical use).
    const uint64_t nNetworkWeight = static_cast<uint64_t>(GetPoSKernelPS());
    if (nWeight == 0 || nNetworkWeight == 0)
    {
        return tr("Wallet is actively staking");
    }

    // Integer arithmetic.  GetTargetSpacing * nNetworkWeight could
    // theoretically overflow uint64 on extreme network-weight values, but
    // realistic ranges keep this well within bounds (120 * 10^15 = 10^17;
    // uint64 max ~1.8 * 10^19).
    const uint64_t nExpectedSecs =
        static_cast<uint64_t>(GetTargetSpacing) * nNetworkWeight / nWeight;

    QString sExpected;
    if (nExpectedSecs >= 86400)
    {
        sExpected = tr("%1d %2h")
            .arg(static_cast<qulonglong>(nExpectedSecs / 86400))
            .arg(static_cast<qulonglong>((nExpectedSecs % 86400) / 3600));
    }
    else if (nExpectedSecs >= 3600)
    {
        sExpected = tr("%1h %2m")
            .arg(static_cast<qulonglong>(nExpectedSecs / 3600))
            .arg(static_cast<qulonglong>((nExpectedSecs % 3600) / 60));
    }
    else if (nExpectedSecs >= 60)
    {
        sExpected = tr("%1m %2s")
            .arg(static_cast<qulonglong>(nExpectedSecs / 60))
            .arg(static_cast<qulonglong>(nExpectedSecs % 60));
    }
    else
    {
        sExpected = tr("%1s").arg(static_cast<qulonglong>(nExpectedSecs));
    }

    return tr("Wallet is actively staking. Expected time between "
              "blocks at current weight and network stake: %1.")
              .arg(sExpected);
}

DigitalNoteGUI::StakingIconState
DigitalNoteGUI::ComputeStakingIconStatePhaseA(
    bool fIBD, bool fWalletLocked, QString &tooltipOut) const
{
    if (fWalletLocked)
    {
        tooltipOut = tr("Not staking because wallet is locked");
        return StakingIconState::None;
    }
    if (!GetBoolArg("-staking", true))
    {
        tooltipOut = tr("Not staking -- disabled in configuration");
        return StakingIconState::None;
    }
    if (nWeight == 0)
    {
        // v2.0.0.8 CW6: distinguish "actively staking, coins in maturity
        // window" from "no stakeable balance at all".
        //
        // A healthy frequent staker whose stake-frequency exceeds
        // nStakeMinConfirmations x target spacing will have all its
        // coinstake outputs mid-maturity at any given moment, producing
        // nWeight=0 even though the wallet is actively staking.  The
        // "Stake" balance (immature coinstake outputs from recent
        // stakes) is the distinguishing signal: positive iff the
        // wallet has staked recently.
        //
        // CW6 v1.1: Guard the GetStake() call with fWalletLoadComplete
        // and pwalletMain non-null.  Pre-load (during GUI construction,
        // before init.cpp sets fWalletLoadComplete=true at line 1713),
        // pwalletMain may be null and mapWallet may be partially
        // populated.  Walking it would either dereference null or
        // corrupt cache state (same trap that motivated the guard in
        // updateWeight() above and in walletmodel.cpp:283).  When
        // pre-load, fall through to the original "no mature coins"
        // message -- correct enough for the transient pre-load window.
        CAmount nStake = 0;
        if (fWalletLoadComplete && pwalletMain)
            nStake = pwalletMain->GetStake();
        
        if (nStake > 0)
        {
            tooltipOut = tr(
                "Recently staked.  All stakeable coins are in the "
                "stake-maturity window and will become stakeable "
                "again as they mature."
            );
            return StakingIconState::Clock;
        }
        tooltipOut = tr("Not staking because you don't have mature coins");
        return StakingIconState::None;
    }
    if (fIBD)
    {
        tooltipOut = tr("Not staking because wallet is syncing");
        return StakingIconState::Clock;
    }
    if (vNodes.empty())
    {
        tooltipOut = tr("Not staking because wallet is offline");
        return StakingIconState::Clock;
    }

    const int64_t loopAge = GetTime() - nLastStakeLoopTime.load();
    if (loopAge > STAKE_LOOP_FRESHNESS_SECS_INITIAL)
    {
        tooltipOut = tr("Staking thread not responding -- restart wallet");
        return StakingIconState::None;
    }

    if (!fLastStakeLoopProductive.load())
    {
        tooltipOut = tr(
            "Staking is starting up.  Stakeable weight is present, "
            "but the most recent staking-loop iteration deferred "
            "(typically waiting for the masternode vote queue, "
            "post-activation).");
        return StakingIconState::Clock;
    }

    tooltipOut = ComputeHammerTooltip();
    return StakingIconState::Hammer;
}

DigitalNoteGUI::StakingIconState
DigitalNoteGUI::ComputeStakingIconStatePhaseB(
    bool fIBD, bool fWalletLocked, QString &tooltipOut) const
{
    if (fWalletLocked)
    {
        tooltipOut = tr("Not staking because wallet is locked");
        return StakingIconState::None;
    }
    if (!GetBoolArg("-staking", true))
    {
        tooltipOut = tr("Not staking -- disabled in configuration");
        return StakingIconState::None;
    }
    if (nWeight == 0)
    {
        // v2.0.0.8 CW6: see ComputeStakingIconStatePhaseA for rationale.
        // CW6 v1.1: fWalletLoadComplete + pwalletMain guard same as Phase A.
        CAmount nStake = 0;
        if (fWalletLoadComplete && pwalletMain)
            nStake = pwalletMain->GetStake();
        
        if (nStake > 0)
        {
            tooltipOut = tr(
                "Recently staked.  All stakeable coins are in the "
                "stake-maturity window and will become stakeable "
                "again as they mature."
            );
            return StakingIconState::Clock;
        }
        tooltipOut = tr("Not staking because you don't have mature coins");
        return StakingIconState::None;
    }
    if (fIBD)
    {
        tooltipOut = tr("Not staking because wallet is syncing");
        return StakingIconState::Clock;
    }
    if (vNodes.empty())
    {
        tooltipOut = tr("Not staking because wallet is offline");
        return StakingIconState::Clock;
    }

    // Safety floor: 5-minute heartbeat staleness drops the latch.
    const int64_t loopAge = GetTime() - nLastStakeLoopTime.load();
    if (loopAge > STAKE_LOOP_FRESHNESS_SECS_LATCHED)
    {
        tooltipOut = tr("Staking thread not responding -- restart wallet");
        return StakingIconState::None;
    }

    tooltipOut = ComputeHammerTooltip();
    return StakingIconState::Hammer;
}

void DigitalNoteGUI::updateStakingIcon()
{
    updateWeight();

    // Avoid taking cs_main directly from a Qt timer thread: IsInitialBlockDownload()
    // and pwalletMain->IsLocked() each acquire heavy locks, and if held by a slow
    // operation (ProcessMessages, block connect, the ThreadStakeMiner queue probe
    // under §29's cs_main hold) the GUI thread blocks for the duration -- triggers
    // "Not Responding" and freezes the wallet UI.  Use TRY_LOCK and bail out on
    // contention -- the icon refreshes again on the next timer fire when locks are
    // free.  Matches the precedent in updateWeight() just above.
    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
        return;

    bool fIBD = IsInitialBlockDownload();
    bool fWalletLocked = false;
    if (pwalletMain)
    {
        TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
        // If we can't get cs_wallet, skip this refresh rather than risk a stale
        // answer -- next timer fire will retry when the lock is free.
        if (!lockWallet)
            return;
        fWalletLocked = pwalletMain->IsLocked();
    }

    QString tooltip;
    StakingIconState state;

    if (m_bHammerLatched)
    {
        // Phase B: only check invalidating events + safety floor.
        state = ComputeStakingIconStatePhaseB(fIBD, fWalletLocked, tooltip);
        if (state != StakingIconState::Hammer)
        {
            // An invalidating event fired; drop the latch.  Next tick will
            // re-evaluate via Phase A.
            m_bHammerLatched = false;
        }
    }
    else
    {
        // Phase A: full prerequisite walk including loop-productivity.
        state = ComputeStakingIconStatePhaseA(fIBD, fWalletLocked, tooltip);
        if (state == StakingIconState::Hammer)
        {
            // First-time entry into Hammer; latch.
            m_bHammerLatched = true;
        }
    }

    switch (state)
    {
        case StakingIconState::Hammer:
            labelStakingIcon->setPixmap(QIcon(":/icons/staking_on")
                .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
            break;
        case StakingIconState::Clock:
            labelStakingIcon->setPixmap(QIcon(":/icons/staking_wait")
                .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
            break;
        case StakingIconState::None:
            labelStakingIcon->setPixmap(QIcon(":/icons/staking_off")
                .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
            break;
    }
    labelStakingIcon->setToolTip(tooltip);
}

void DigitalNoteGUI::detectShutdown()
{
    if (ShutdownRequested())
        QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
}

void DigitalNoteGUI::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
        // A9: rescan/import just finished -- emit summary toast for
        // any transactions whose individual toasts were suppressed
        // during the batch.  Defer to the end of the event queue
        // because the drain that was triggered by the same
        // ShowProgress(100) call has queued many updateTransaction
        // events ahead of us; those events drive incomingTransaction
        // which is where the per-tx counter increments.  Without the
        // defer, we'd fire the summary with a stale (zero) counter.
        QTimer::singleShot(0, this, SLOT(maybeEmitBatchSummary()));
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}

void DigitalNoteGUI::editConfig()
{
    EditConfigDialog dlg;
    dlg.setModel(clientModel);
    dlg.exec();
}

void DigitalNoteGUI::editConfigExt()
{
    boost::filesystem::path path = GetConfigFile();
    QString pathString = QString::fromStdString(path.string());
    QDesktopServices::openUrl(QUrl::fromLocalFile(pathString));
}

void DigitalNoteGUI::openDataDir()
{
    boost::filesystem::path path = GetDataDir();
    QString pathString = QString::fromStdString(path.string());
    QDesktopServices::openUrl(QUrl::fromLocalFile(pathString));
}

void DigitalNoteGUI::showSeedPhrase()
{
    if (!walletModel)
        return;

    if (!seedPhraseDialog)
        seedPhraseDialog = new SeedPhraseDialog(walletModel, this);

    seedPhraseDialog->clearMnemonic();
    seedPhraseDialog->exec();
}