#include "lockedoutputsdialog.h"
#include "ui_lockedoutputsdialog.h"

#include "walletmodel.h"
#include "bitcoinunits.h"
#include "coutpoint.h"

#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidgetItem>

#include <vector>

namespace {

// Constants for what context-menu actions need to remember.
const int COL_LOCK = 0;
const int COL_ADDRESS = 1;
const int COL_LABEL = 2;
const int COL_AMOUNT = 3;
const int COL_TYPE = 4;
const int COL_TXID = 5;

// Custom roles on column-0 items so action handlers can recover the
// underlying outpoint and tier without parsing the visible text.
const int ROLE_TXID = Qt::UserRole + 1;
const int ROLE_VOUT = Qt::UserRole + 2;
const int ROLE_TIER = Qt::UserRole + 3;
const int ROLE_MN_ALIAS = Qt::UserRole + 4;
const int ROLE_AMOUNT = Qt::UserRole + 5;

}

LockedOutputsDialog::LockedOutputsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LockedOutputsDialog),
    walletModel(nullptr),
    contextMenuRow(-1)
{
    ui->setupUi(this);

    // Column sizing: every column to ResizeToContents; the TXID:vout
    // column gets to grow to fit its 64-char hash.  No stretchLastSection
    // (the table just gets a horizontal scrollbar if window is narrower
    // than the natural width).
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableWidget->horizontalHeader()->setStretchLastSection(false);

    // Add cell padding so columns aren't packed tight.
    ui->tableWidget->setStyleSheet(
        "QTableWidget::item { padding-left: 8px; padding-right: 8px; }");

    connect(ui->tableWidget, SIGNAL(cellClicked(int, int)),
            this, SLOT(onCellClicked(int, int)));
    connect(ui->tableWidget, SIGNAL(cellDoubleClicked(int, int)),
            this, SLOT(onCellDoubleClicked(int, int)));
    connect(ui->tableWidget, SIGNAL(customContextMenuRequested(const QPoint&)),
            this, SLOT(onCustomContextMenu(const QPoint&)));
}

LockedOutputsDialog::~LockedOutputsDialog()
{
    delete ui;
}

void LockedOutputsDialog::setWalletModel(WalletModel *model)
{
    walletModel = model;
    refresh();
}

void LockedOutputsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    refresh();
}

QString LockedOutputsDialog::humanAmount(qint64 satoshis) const
{
    return DigitalNoteUnits::formatWithUnit(DigitalNoteUnits::XDN, satoshis);
}

QString LockedOutputsDialog::tierTypeText(int tier, const QString& mnAlias) const
{
    switch (tier)
    {
        case LockedOutputDetail::LOT_MASTERNODE:
            return tr("Masternode: %1").arg(mnAlias);
        case LockedOutputDetail::LOT_MN_COLLATERAL_AMOUNT:
            return tr("2M XDN (not in masternode.conf)");
        default:
            return tr("Other locked output");
    }
}

void LockedOutputsDialog::refresh()
{
    ui->tableWidget->setRowCount(0);

    if (!walletModel)
    {
        ui->statusLabel->setText(tr("No wallet."));
        return;
    }

    std::vector<LockedOutputDetail> details;
    walletModel->listLockedOutputsWithDetails(details);

    if (details.empty())
    {
        ui->statusLabel->setText(tr("No outputs are currently locked."));
        return;
    }

    qint64 totalSatoshis = 0;

    for (const LockedOutputDetail& d : details)
    {
        int row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);

        // Column 0: lock-state indicator.  Always shown as locked
        // since rows here are by definition currently locked.
        // Click toggles via cellClicked.  The padlock icon set on
        // the item is the visual cue; the "Locked" text is supplemental.
        QTableWidgetItem *lockItem = new QTableWidgetItem(tr("Locked"));
        lockItem->setIcon(QIcon(":/icons/lock_closed_solid"));
        lockItem->setFlags(lockItem->flags() & ~Qt::ItemIsEditable);
        // Stash the outpoint and classification on column 0 so context
        // menu actions and toggle handlers can recover them.
        lockItem->setData(ROLE_TXID,
            QString::fromStdString(d.outpoint.hash.ToString()));
        lockItem->setData(ROLE_VOUT, static_cast<uint>(d.outpoint.n));
        lockItem->setData(ROLE_TIER, static_cast<int>(d.tier));
        lockItem->setData(ROLE_MN_ALIAS, d.masternodeAlias);
        lockItem->setData(ROLE_AMOUNT, static_cast<qlonglong>(d.amount));
        ui->tableWidget->setItem(row, COL_LOCK, lockItem);

        QTableWidgetItem *addrItem = new QTableWidgetItem(d.address);
        addrItem->setFlags(addrItem->flags() & ~Qt::ItemIsEditable);
        ui->tableWidget->setItem(row, COL_ADDRESS, addrItem);

        QTableWidgetItem *labelItem = new QTableWidgetItem(d.addressLabel);
        labelItem->setFlags(labelItem->flags() & ~Qt::ItemIsEditable);
        ui->tableWidget->setItem(row, COL_LABEL, labelItem);

        QTableWidgetItem *amountItem = new QTableWidgetItem(humanAmount(d.amount));
        amountItem->setFlags(amountItem->flags() & ~Qt::ItemIsEditable);
        amountItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ui->tableWidget->setItem(row, COL_AMOUNT, amountItem);

        QTableWidgetItem *typeItem = new QTableWidgetItem(tierTypeText(d.tier, d.masternodeAlias));
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        ui->tableWidget->setItem(row, COL_TYPE, typeItem);

        QString txidVout = QString::fromStdString(d.outpoint.hash.ToString())
                          + QStringLiteral(":")
                          + QString::number(d.outpoint.n);
        QTableWidgetItem *txItem = new QTableWidgetItem(txidVout);
        txItem->setFlags(txItem->flags() & ~Qt::ItemIsEditable);
        txItem->setFont(QFont(QStringLiteral("monospace")));
        ui->tableWidget->setItem(row, COL_TXID, txItem);

        totalSatoshis += d.amount;
    }

    ui->statusLabel->setText(tr("%1 output(s) locked, total %2")
        .arg(details.size())
        .arg(humanAmount(totalSatoshis)));
}

void LockedOutputsDialog::toggleLockForRow(int row)
{
    if (row < 0 || row >= ui->tableWidget->rowCount() || !walletModel)
        return;

    QTableWidgetItem *lockItem = ui->tableWidget->item(row, COL_LOCK);
    if (!lockItem)
        return;

    // Recover outpoint + tier from the stashed data
    QString txidStr = lockItem->data(ROLE_TXID).toString();
    uint vout = lockItem->data(ROLE_VOUT).toUInt();
    int tier = lockItem->data(ROLE_TIER).toInt();
    QString mnAlias = lockItem->data(ROLE_MN_ALIAS).toString();

    // All rows in this dialog are by definition currently locked.
    // Toggle = unlock.  Show the appropriately-severe confirmation.
    QMessageBox::StandardButton confirm;
    switch (tier)
    {
        case LockedOutputDetail::LOT_MASTERNODE:
            confirm = QMessageBox::warning(this, tr("Unlock masternode collateral?"),
                tr("This output is the collateral for a masternode (\"%1\") "
                   "according to your masternode configuration.\n\n"
                   "Unlocking it allows it to be spent -- including being "
                   "chosen as a stake input. If this output is spent or "
                   "staked, the masternode will permanently fail.\n\n"
                   "Are you sure you want to unlock this output?").arg(mnAlias),
                QMessageBox::Cancel | QMessageBox::Yes,
                QMessageBox::Cancel);
            break;
        case LockedOutputDetail::LOT_MN_COLLATERAL_AMOUNT:
            confirm = QMessageBox::warning(this, tr("Unlock 2M XDN output?"),
                tr("This output is the masternode collateral amount "
                   "(2,000,000 XDN) but is not listed in your masternode "
                   "configuration. It may be a former masternode collateral, "
                   "or one you set aside for future use.\n\n"
                   "Unlocking allows it to be spent or staked. Are you sure?"),
                QMessageBox::Cancel | QMessageBox::Yes,
                QMessageBox::Cancel);
            break;
        default:
            confirm = QMessageBox::question(this, tr("Unlock this output?"),
                tr("Unlocking allows this output to be spent by sends or stakes."),
                QMessageBox::Cancel | QMessageBox::Yes,
                QMessageBox::Cancel);
            break;
    }

    if (confirm != QMessageBox::Yes)
        return;

    uint256 hash;
    hash.SetHex(txidStr.toStdString());
    COutPoint out(hash, vout);
    walletModel->unlockCoin(out);

    // Refresh -- the unlocked row simply disappears from the table
    // (we only show locked outputs).
    refresh();
}

void LockedOutputsDialog::onCellClicked(int row, int column)
{
    // Click on the Lock column toggles state.  Other columns do nothing.
    if (column == COL_LOCK)
        toggleLockForRow(row);
}

void LockedOutputsDialog::onCellDoubleClicked(int row, int column)
{
    // Any cell double-clicked toggles the lock on that row.
    Q_UNUSED(column);
    toggleLockForRow(row);
}

void LockedOutputsDialog::onCustomContextMenu(const QPoint &pos)
{
    QTableWidgetItem *item = ui->tableWidget->itemAt(pos);
    if (!item)
    {
        contextMenuRow = -1;
        return;
    }

    contextMenuRow = item->row();

    // Right-click implies focus on this row -- match the
    // masternodemanager pattern so visible selection matches what
    // actions will operate on.
    ui->tableWidget->clearSelection();
    ui->tableWidget->selectRow(contextMenuRow);

    QMenu menu(this);
    QAction *copyTxIdAction = menu.addAction(tr("Copy transaction ID"));
    QAction *copyAddressAction = menu.addAction(tr("Copy address"));
    QAction *copyAmountAction = menu.addAction(tr("Copy amount"));
    menu.addSeparator();
    QAction *toggleAction = menu.addAction(tr("Unlock collateral"));

    connect(copyTxIdAction, SIGNAL(triggered()), this, SLOT(onCopyTxId()));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(onCopyAddress()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(onCopyAmount()));
    connect(toggleAction, SIGNAL(triggered()), this, SLOT(onContextLockUnlock()));

    menu.exec(ui->tableWidget->viewport()->mapToGlobal(pos));
}

void LockedOutputsDialog::onCopyTxId()
{
    if (contextMenuRow < 0)
        return;
    QTableWidgetItem *lockItem = ui->tableWidget->item(contextMenuRow, COL_LOCK);
    if (!lockItem)
        return;
    QApplication::clipboard()->setText(lockItem->data(ROLE_TXID).toString());
}

void LockedOutputsDialog::onCopyAddress()
{
    if (contextMenuRow < 0)
        return;
    QTableWidgetItem *addrItem = ui->tableWidget->item(contextMenuRow, COL_ADDRESS);
    if (!addrItem)
        return;
    QApplication::clipboard()->setText(addrItem->text());
}

void LockedOutputsDialog::onCopyAmount()
{
    if (contextMenuRow < 0)
        return;
    QTableWidgetItem *lockItem = ui->tableWidget->item(contextMenuRow, COL_LOCK);
    if (!lockItem)
        return;
    qlonglong satoshis = lockItem->data(ROLE_AMOUNT).toLongLong();
    QApplication::clipboard()->setText(humanAmount(satoshis));
}

void LockedOutputsDialog::onContextLockUnlock()
{
    toggleLockForRow(contextMenuRow);
}
