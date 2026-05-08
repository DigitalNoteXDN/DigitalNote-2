#include "compat.h"

#include <boost/lexical_cast.hpp>
#include <fstream>
#include <QAbstractItemDelegate>
#include <QClipboard>
#include <QPainter>
#include <QTimer>
#include <QDebug>
#include <QScrollArea>
#include <QScroller>
#include <QDateTime>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QItemSelectionModel>
#include <QDesktopServices>
#include <QTabBar>

#if QT_VERSION < 0x050000
#include <QUrl>
#else
#include <QUrlQuery>
#endif

#include "clientmodel.h"
#include "walletmodel.h"
#include "cmasternode.h"
#include "cmasternodeman.h"
#include "masternodeman.h"
#include "cactivemasternode.h"
#include "cmasternodeconfig.h"
#include "cmasternodeconfigentry.h"
#include "masternodeconfig.h"
#include "masternode_extern.h"
#include "mnengine_extern.h"
#include "walletdb.h"
#include "init.h"
#include "rpcserver.h"
#include "guiutil.h"
#include "script.h"
#include "cdigitalnoteaddress.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"
#include "thread.h"
#include "masternodeworker.h"

#include "masternodemanager.h"
#include "ui_masternodemanager.h"
#include "addeditadrenalinenode.h"
#include "adrenalinenodeconfigdialog.h"
#include "coutpoint.h"
#include "uint/uint256.h"

MasternodeManager::MasternodeManager(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MasternodeManager),
    clientModel(0),
    walletModel(0),
    ownContextMenuRow(-1)
{
    ui->setupUi(this);

    ui->editButton->setEnabled(false);
    ui->startButton->setEnabled(false);
    ui->stopButton->setEnabled(false);

    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMasternodes->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetMasternodes->setColumnWidth(1, columnProtocolWidth);
    ui->tableWidgetMasternodes->setColumnWidth(2, columnStatusWidth);
    ui->tableWidgetMasternodes->setColumnWidth(3, columnActiveWidth);
    ui->tableWidgetMasternodes->setColumnWidth(4, columnLastSeenWidth);

    ui->tableWidgetMasternodes->setContextMenuPolicy(Qt::CustomContextMenu);
    QAction *copyAddressAction = new QAction(tr("Copy Address"), this);
    QAction *copyPubkeyAction = new QAction(tr("Copy Pubkey"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyPubkeyAction);
    connect(ui->tableWidgetMasternodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyPubkeyAction, SIGNAL(triggered()), this, SLOT(copyPubkey()));

    // B2: own-masternodes context menu with Lock/Unlock collateral.
    // Enable state is set just before the menu is shown by
    // showOwnContextMenu(), based on the current row's lock state.
    ui->tableWidget_2->setContextMenuPolicy(Qt::CustomContextMenu);
    lockCollateralAction   = new QAction(tr("Lock collateral"),   this);
    unlockCollateralAction = new QAction(tr("Unlock collateral"), this);
    ownContextMenu = new QMenu();
    ownContextMenu->addAction(lockCollateralAction);
    ownContextMenu->addAction(unlockCollateralAction);
    connect(ui->tableWidget_2, SIGNAL(customContextMenuRequested(const QPoint&)),
            this, SLOT(showOwnContextMenu(const QPoint&)));
    connect(lockCollateralAction,   SIGNAL(triggered()),
            this, SLOT(lockSelectedCollateral()));
    connect(unlockCollateralAction, SIGNAL(triggered()),
            this, SLOT(unlockSelectedCollateral()));

    ui->tableWidgetMasternodes->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableWidget_2->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // Override stretchLastSection (set in masternodemanager.ui).  With
    // ResizeToContents on every column, stretching the last column
    // would just give Lock excess whitespace.  Disable so columns fit
    // their content without the rightmost one ballooning.  Add cell
    // padding via stylesheet so columns aren't packed tight against
    // their neighbours.
    ui->tableWidget_2->horizontalHeader()->setStretchLastSection(false);
    ui->tableWidget_2->setStyleSheet(
        "QTableWidget::item { padding-left: 8px; padding-right: 8px; }");

    // Style the page's two-tab header ("DigitalNote Network" / "My
    // Master Nodes") to match the Transactions page's WatchOnly tab
    // bar: bold-when-selected with generous padding and matching
    // min-width.  Keeps a consistent look across the wallet's
    // section-header tab bars.  See transactionview.cpp's similar
    // setStyleSheet call for the reference implementation.
    ui->tabWidget->tabBar()->setDocumentMode(true);
    ui->tabWidget->tabBar()->setExpanding(false);
    ui->tabWidget->tabBar()->setStyleSheet(
        "QTabBar::tab {"
        "  padding: 8px 28px;"
        "  min-width: 140px;"
        "  font-size: 13px;"
        "}"
        "QTabBar::tab:selected {"
        "  font-weight: bold;"
        "  border-bottom: 2px solid palette(highlight);"
        "}"
        "QTabBar::tab:!selected {"
        "  color: palette(mid);"
        "}");

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    if(!GetBoolArg("-reindexaddr", false))
        timer->start(30000);

    updateNodeList();

}

void MasternodeManager::on_tabWidget_currentChanged(int index)
{
    if (index == 1)
    {
        on_UpdateButton_clicked();
    }
}

MasternodeManager::~MasternodeManager()
{
    delete ui;
}

void MasternodeManager::on_tableWidget_2_itemSelectionChanged()
{
    int n = ui->tableWidget_2->selectionModel()->selectedRows().count();
    ui->startButton->setEnabled(n > 0);
    ui->stopButton->setEnabled(n > 0);
    // Edit operates on exactly one row -- multi-row edit isn't supported.
    ui->editButton->setEnabled(n == 1);
}

void MasternodeManager::updateAdrenalineNode(QString alias, QString addr, QString privkey, QString txHash, QString txIndex, QString status)
{
    LOCK(cs_adrenaline);
    bool bFound = false;
    int nodeRow = 0;
    for(int i=0; i < ui->tableWidget_2->rowCount(); i++)
    {
        if(ui->tableWidget_2->item(i, 0)->text() == alias)
        {
            bFound = true;
            nodeRow = i;
            break;
        }
    }

    if(nodeRow == 0 && !bFound)
        ui->tableWidget_2->insertRow(0);

    QTableWidgetItem *aliasItem = new QTableWidgetItem(alias);
    QTableWidgetItem *addrItem = new QTableWidgetItem(addr);
    QTableWidgetItem *statusItem = new QTableWidgetItem(status);

    // B2: stash the collateral COutPoint on the alias item so the
    // context menu slots can read it back without re-walking
    // masternode.conf.  Two roles (one for txid, one for vout) avoid
    // having to parse a combined string later.
    aliasItem->setData(Qt::UserRole,     txHash);
    aliasItem->setData(Qt::UserRole + 1, txIndex);

    ui->tableWidget_2->setItem(nodeRow, 0, aliasItem);
    ui->tableWidget_2->setItem(nodeRow, 1, addrItem);
    ui->tableWidget_2->setItem(nodeRow, 2, statusItem);

    // B2: Collateral column.  Reads CWallet::IsLockedCoin via the
    // wallet model.  Centralised in refreshCollateralCell so the
    // lock/unlock slots can just call it after toggling.
    refreshCollateralCell(nodeRow);
}

// B2: read the lock state for one row's collateral UTXO and update the
// fourth column.  Pulls (txHash, txIndex) from the alias item's user
// data (set by updateAdrenalineNode) so we don't re-walk
// masternode.conf on every refresh.
void MasternodeManager::refreshCollateralCell(int row)
{
    if (!walletModel || row < 0 || row >= ui->tableWidget_2->rowCount())
        return;
    QTableWidgetItem *aliasItem = ui->tableWidget_2->item(row, 0);
    if (!aliasItem)
        return;
    QString txHashStr = aliasItem->data(Qt::UserRole).toString();
    QString txIndexStr = aliasItem->data(Qt::UserRole + 1).toString();
    if (txHashStr.isEmpty())
        return;

    uint256 hash;
    hash.SetHex(txHashStr.toStdString());
    bool ok = false;
    int vout = txIndexStr.toInt(&ok);
    if (!ok || vout < 0)
        return;

    bool locked = walletModel->isLockedCoin(hash, static_cast<unsigned int>(vout));

    // Plain text rather than emoji glyphs so this renders consistently
    // on Windows MSYS2 builds where Qt's default font may not have
    // colour-emoji coverage.  The leading bullet keeps a visible glyph
    // even on minimal fonts.
    QTableWidgetItem *cell = new QTableWidgetItem(
        locked ? tr("\xE2\x97\x8F  Locked")     // U+25CF BLACK CIRCLE
               : tr("\xE2\x97\x8B  Unlocked")); // U+25CB WHITE CIRCLE
    cell->setToolTip(locked
        ? tr("This collateral UTXO is locked and cannot be spent by\n"
             "stakes or regular sends.  Right-click to unlock.")
        : tr("This collateral UTXO is NOT locked.  It could be spent\n"
             "by a stake reward or a regular send, which would\n"
             "destroy the masternode.  Right-click to lock."));
    ui->tableWidget_2->setItem(row, 3, cell);
}

void MasternodeManager::showOwnContextMenu(const QPoint &point)
{
    QTableWidgetItem *item = ui->tableWidget_2->itemAt(point);
    if (!item)
    {
        ownContextMenuRow = -1;
        return;
    }

    int row = item->row();
    QTableWidgetItem *aliasItem = ui->tableWidget_2->item(row, 0);
    if (!aliasItem || !walletModel)
    {
        ownContextMenuRow = -1;
        return;
    }

    // Look up current lock state to choose which action to enable.
    QString txHashStr = aliasItem->data(Qt::UserRole).toString();
    QString txIndexStr = aliasItem->data(Qt::UserRole + 1).toString();
    if (txHashStr.isEmpty())
    {
        ownContextMenuRow = -1;
        return;
    }
    uint256 hash;
    hash.SetHex(txHashStr.toStdString());
    bool ok = false;
    int vout = txIndexStr.toInt(&ok);
    if (!ok || vout < 0)
    {
        ownContextMenuRow = -1;
        return;
    }

    bool locked = walletModel->isLockedCoin(hash, static_cast<unsigned int>(vout));
    lockCollateralAction->setEnabled(!locked);
    unlockCollateralAction->setEnabled(locked);

    // Stash the row so the action handlers act on the right-clicked
    // row (not on whatever happens to be selected).  Selection-based
    // dispatch was the previous behaviour and led to "right-click row
    // 5 / pick Unlock / unlock fires on row 1" surprises.
    ownContextMenuRow = row;

    // Right-click implies focus on this row.  Replace any existing
    // multi-selection so the visual selection matches what the action
    // handlers (and the Stop/Start/Edit buttons) will operate on.
    // Without this, users with N rows selected can right-click an
    // unrelated row and end up confused about whether the menu acts
    // on the click target or the selection.
    ui->tableWidget_2->clearSelection();
    ui->tableWidget_2->selectRow(row);

    ownContextMenu->exec(ui->tableWidget_2->viewport()->mapToGlobal(point));
}

void MasternodeManager::lockSelectedCollateral()
{
    int row = ownContextMenuRow;
    if (row < 0 || row >= ui->tableWidget_2->rowCount() || !walletModel)
        return;
    QTableWidgetItem *aliasItem = ui->tableWidget_2->item(row, 0);
    if (!aliasItem)
        return;

    QString txHashStr = aliasItem->data(Qt::UserRole).toString();
    QString txIndexStr = aliasItem->data(Qt::UserRole + 1).toString();
    uint256 hash;
    hash.SetHex(txHashStr.toStdString());
    bool ok = false;
    int vout = txIndexStr.toInt(&ok);
    if (!ok || vout < 0)
        return;

    COutPoint out(hash, static_cast<unsigned int>(vout));
    walletModel->lockCoin(out);
    refreshCollateralCell(row);
}

void MasternodeManager::unlockSelectedCollateral()
{
    int row = ownContextMenuRow;
    if (row < 0 || row >= ui->tableWidget_2->rowCount() || !walletModel)
        return;
    QTableWidgetItem *aliasItem = ui->tableWidget_2->item(row, 0);
    if (!aliasItem)
        return;

    QString txHashStr = aliasItem->data(Qt::UserRole).toString();
    QString txIndexStr = aliasItem->data(Qt::UserRole + 1).toString();
    uint256 hash;
    hash.SetHex(txHashStr.toStdString());
    bool ok = false;
    int vout = txIndexStr.toInt(&ok);
    if (!ok || vout < 0)
        return;

    COutPoint out(hash, static_cast<unsigned int>(vout));
    walletModel->unlockCoin(out);
    refreshCollateralCell(row);
}

static QString seconds_to_DHMS(quint32 duration)
{
	QString res;
	int seconds = (int) (duration % 60);
	duration /= 60;
	int minutes = (int) (duration % 60);
	duration /= 60;
	int hours = (int) (duration % 24);
	int days = (int) (duration / 24);
	
	if((hours == 0)&&(days == 0))
	{
		return res.asprintf("%02dm:%02ds", minutes, seconds);
	}
	
	if (days == 0)
	{
		return res.asprintf("%02dh:%02dm:%02ds", hours, minutes, seconds);
	}
	
	return res.asprintf("%dd %02dh:%02dm:%02ds", days, hours, minutes, seconds);
}

void MasternodeManager::updateNodeList()
{
	static int64_t nTimeListUpdated = GetTime();
	int64_t nSecondsToWait = nTimeListUpdated - GetTime() + 30;
	
	if (nSecondsToWait > 0)
	{
		return;
	}
	
	TRY_LOCK(cs_masternodes, lockMasternodes);
	
	if(!lockMasternodes)
	{
		return;
	}
	
	ui->countLabel->setText("Updating...");
	ui->tableWidgetMasternodes->setSortingEnabled(false);
	ui->tableWidgetMasternodes->clearContents();
	ui->tableWidgetMasternodes->setRowCount(0);
	std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeVector();

	for(CMasternode& mn : vMasternodes)
	{
		int mnRow = 0;
		ui->tableWidgetMasternodes->insertRow(0);

		// populate list
		// Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
		QTableWidgetItem* addressItem = new QTableWidgetItem(QString::fromStdString(mn.addr.ToString()));
		QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(mn.protocolVersion));
		QTableWidgetItem* statusItem = new QTableWidgetItem(QString::number(mn.IsEnabled()));
		QTableWidgetItem* activeSecondsItem = new QTableWidgetItem(seconds_to_DHMS((qint64)(mn.lastTimeSeen - mn.sigTime)));
		QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat(mn.lastTimeSeen)));

		CScript pubkey;
		pubkey =GetScriptForDestination(mn.pubkey.GetID());
		CTxDestination address1;
		ExtractDestination(pubkey, address1);
		CDigitalNoteAddress address2(address1);
		QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(address2.ToString()));

		ui->tableWidgetMasternodes->setItem(mnRow, 0, addressItem);
		ui->tableWidgetMasternodes->setItem(mnRow, 1, protocolItem);
		ui->tableWidgetMasternodes->setItem(mnRow, 2, statusItem);
		ui->tableWidgetMasternodes->setItem(mnRow, 3, activeSecondsItem);
		ui->tableWidgetMasternodes->setItem(mnRow, 4, lastSeenItem);
		ui->tableWidgetMasternodes->setItem(mnRow, 5, pubkeyItem);
	}

	ui->countLabel->setText(QString::number(ui->tableWidgetMasternodes->rowCount()));
	ui->tableWidgetMasternodes->setSortingEnabled(true);
}


void MasternodeManager::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
    }
}

void MasternodeManager::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
    }

}

// Populate the My Master Nodes table whenever the page becomes
// visible.  Without this, navigating to the page when its QTabWidget
// already has My Master Nodes selected (e.g. it's the last-used tab)
// doesn't emit currentChanged, and the table only fills the next time
// the user touches a tab or the Update button.  Calling
// on_UpdateButton_clicked() unconditionally on show is cheap -- it
// walks masternodeConfig.getEntries() (one row per configured MN) and
// calls updateAdrenalineNode for each, which is what already happens
// on every tab change today.
void MasternodeManager::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    on_UpdateButton_clicked();
}

void MasternodeManager::on_createButton_clicked()
{
    AddEditAdrenalineNode* aenode = new AddEditAdrenalineNode();
    aenode->exec();
}


static void spawnMasternodeWorker(MasternodeManager* mgr,
                                   MasternodeWorker::Operation op,
                                   std::vector<CMasternodeConfigEntry> entries)
{
    mgr->setButtonsEnabled(false);
    QThread *t = new QThread(mgr);
    MasternodeWorker *w = new MasternodeWorker(op, std::move(entries));
    w->moveToThread(t);
    QObject::connect(t,    &QThread::started,              w,   &MasternodeWorker::run);
    QObject::connect(w,    &MasternodeWorker::finished,    mgr, &MasternodeManager::onWorkerFinished);
    QObject::connect(w,    &MasternodeWorker::error,       mgr, &MasternodeManager::onWorkerError);
    QObject::connect(w,    &MasternodeWorker::finished,    t,   &QThread::quit);
    QObject::connect(w,    &MasternodeWorker::error,       t,   &QThread::quit);
    QObject::connect(t,    &QThread::finished,             t,   &QObject::deleteLater);
    QObject::connect(t,    &QThread::finished,             w,   &QObject::deleteLater);
    t->start();
}

void MasternodeManager::setButtonsEnabled(bool enabled)
{
    ui->startButton->setEnabled(enabled);
    ui->startAllButton->setEnabled(enabled);
    ui->stopButton->setEnabled(enabled);
    ui->stopAllButton->setEnabled(enabled);
    ui->UpdateButton->setEnabled(enabled);
}

void MasternodeManager::onWorkerFinished(QString result)
{
    setButtonsEnabled(true);
    if (!result.isEmpty()) {
        QMessageBox msg;
        msg.setText(result);
        msg.exec();
    }
    on_UpdateButton_clicked();
}

void MasternodeManager::onWorkerError(QString message)
{
    setButtonsEnabled(true);
    QMessageBox::critical(this, tr("Masternode Error"), message);
}

void MasternodeManager::on_startButton_clicked()
{
    QItemSelectionModel* selectionModel = ui->tableWidget_2->selectionModel();
    QModelIndexList selectedRows = selectionModel->selectedRows();

    if (selectedRows.count() == 0) {
        QMessageBox::warning(this, tr("No Selection"), tr("Select a Masternode alias to start."));
        return;
    }

    if (pwalletMain->IsLocked()) {
        QMessageBox::warning(this, tr("Wallet Locked"), tr("Please unlock your wallet to start a Masternode."));
        return;
    }

    std::vector<CMasternodeConfigEntry> entries;
    for (int i = 0; i < selectedRows.count(); i++) {
        int r = selectedRows.at(i).row();
        std::string sAlias = ui->tableWidget_2->item(r, 0)->text().toStdString();
        for (const CMasternodeConfigEntry& mne : masternodeConfig.getEntries()) {
            if (mne.getAlias() == sAlias) {
                entries.push_back(mne);
                break;
            }
        }
    }

    spawnMasternodeWorker(this, MasternodeWorker::StartSelected, std::move(entries));
}

void MasternodeManager::on_startAllButton_clicked()
{
    if (pwalletMain->IsLocked()) {
        QMessageBox::warning(this, tr("Wallet Locked"), tr("Please unlock your wallet to start Masternodes."));
        return;
    }
    std::vector<CMasternodeConfigEntry> entries = masternodeConfig.getEntries();
    spawnMasternodeWorker(this, MasternodeWorker::StartAll, std::move(entries));
}

void MasternodeManager::on_stopButton_clicked()
{
    QItemSelectionModel* selectionModel = ui->tableWidget_2->selectionModel();
    QModelIndexList selectedRows = selectionModel->selectedRows();

    if (selectedRows.count() == 0) {
        QMessageBox::warning(this, tr("No Selection"), tr("Select a Masternode alias to stop."));
        return;
    }

    if (pwalletMain->IsLocked()) {
        QMessageBox::warning(this, tr("Wallet Locked"), tr("Please unlock your wallet to stop a Masternode."));
        return;
    }

    std::vector<CMasternodeConfigEntry> entries;
    for (int i = 0; i < selectedRows.count(); i++) {
        int r = selectedRows.at(i).row();
        std::string sAlias = ui->tableWidget_2->item(r, 0)->text().toStdString();
        for (const CMasternodeConfigEntry& mne : masternodeConfig.getEntries()) {
            if (mne.getAlias() == sAlias) {
                entries.push_back(mne);
                break;
            }
        }
    }

    spawnMasternodeWorker(this, MasternodeWorker::StopSelected, std::move(entries));
}

void MasternodeManager::on_stopAllButton_clicked()
{
    if (pwalletMain->IsLocked()) {
        QMessageBox::warning(this, tr("Wallet Locked"), tr("Please unlock your wallet to stop Masternodes."));
        return;
    }
    std::vector<CMasternodeConfigEntry> entries = masternodeConfig.getEntries();
    spawnMasternodeWorker(this, MasternodeWorker::StopAll, std::move(entries));
}

void MasternodeManager::on_UpdateButton_clicked()
{
	for(CMasternodeConfigEntry mne : masternodeConfig.getEntries())
	{
		std::string errorMessage;
		std::string strDonateAddress = "";
		std::string strDonationPercentage = "";

		std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeVector();
		
		if (errorMessage == "")
		{
			updateAdrenalineNode(
				QString::fromStdString(mne.getAlias()),
				QString::fromStdString(mne.getIp()),
				QString::fromStdString(mne.getPrivKey()),
				QString::fromStdString(mne.getTxHash()),
				QString::fromStdString(mne.getOutputIndex()),
				QString::fromStdString("Not in the masternode list.")
			);
		}
		else
		{
			updateAdrenalineNode(
				QString::fromStdString(mne.getAlias()),
				QString::fromStdString(mne.getIp()),
				QString::fromStdString(mne.getPrivKey()),
				QString::fromStdString(mne.getTxHash()),
				QString::fromStdString(mne.getOutputIndex()),
				QString::fromStdString(errorMessage)
			);
		}

		for(CMasternode& mn : vMasternodes)
		{
			if (mn.addr.ToString().c_str() == mne.getIp())
			{
				updateAdrenalineNode(
					QString::fromStdString(mne.getAlias()),
					QString::fromStdString(mne.getIp()),
					QString::fromStdString(mne.getPrivKey()),
					QString::fromStdString(mne.getTxHash()),
					QString::fromStdString(mne.getOutputIndex()),
					QString::fromStdString("Masternode is Running.")
				);
			}
		}
	}
}

void MasternodeManager::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetMasternodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

void MasternodeManager::copyAddress()
{
    std::string sData;
    int row;
    QItemSelectionModel* selectionModel = ui->tableWidgetMasternodes->selectionModel();
    QModelIndexList selectedRows = selectionModel->selectedRows();
    if(selectedRows.count() == 0)
        return;

    for (int i = 0; i < selectedRows.count(); i++)
    {
        QModelIndex index = selectedRows.at(i);
        row = index.row();
        sData += ui->tableWidgetMasternodes->item(row, 0)->text().toStdString();
        if (i < selectedRows.count()-1)
            sData += "\n";
    }

    QApplication::clipboard()->setText(QString::fromStdString(sData));
}

void MasternodeManager::copyPubkey()
{
    std::string sData;
    int row;
    QItemSelectionModel* selectionModel = ui->tableWidgetMasternodes->selectionModel();
    QModelIndexList selectedRows = selectionModel->selectedRows();
    if(selectedRows.count() == 0)
        return;

    for (int i = 0; i < selectedRows.count(); i++)
    {
        QModelIndex index = selectedRows.at(i);
        row = index.row();
        sData += ui->tableWidgetMasternodes->item(row, 5)->text().toStdString();
        if (i < selectedRows.count()-1)
            sData += "\n";
    }

    QApplication::clipboard()->setText(QString::fromStdString(sData));
}

void MasternodeManager::on_editButton_clicked()
{
    std::string statusObj;

    // load config data
    boost::filesystem::ifstream streamConfig(GetMasternodeConfigFile());
    boost::filesystem::path mnodeConfig(GetDataDir() / "masternode.conf");

    if (!streamConfig.good()) {
        statusObj += "<br>Cannot find MasterNode config file!" ;
        QMessageBox msg;
        msg.setText(QString::fromStdString(statusObj));
        msg.exec();
        streamConfig.close();
        return;
    }

    streamConfig.close();

    /* Open masternode.conf with the associated application */
    if (boost::filesystem::exists(mnodeConfig))
        QDesktopServices::openUrl(QUrl::fromLocalFile(GUIUtil::boostPathToQString(mnodeConfig)));
}