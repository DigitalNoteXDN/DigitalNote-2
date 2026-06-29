#ifndef LOCKEDOUTPUTSDIALOG_H
#define LOCKEDOUTPUTSDIALOG_H

#include "compat.h"

#include <QDialog>
#include <QMenu>
#include <QShowEvent>

namespace Ui {
    class LockedOutputsDialog;
}

class WalletModel;

/** Tools -> Locked Outputs...
 *
 *  A modal dialog listing every output currently held in
 *  setLockedCoins, with per-row toggle (column 0) and a right-click
 *  context menu (Copy txid / Copy address / Copy amount / Lock or
 *  Unlock / Show transaction details).
 *
 *  Outputs are classified into three tiers (see
 *  WalletModel::LockedOutputDetail::Tier):
 *    - configured masternode (label = MN alias)
 *    - 2M XDN matching MN collateral amount but not in masternode.conf
 *    - other locked outputs
 *
 *  Watch-only outputs are filtered out: they are not the user's to
 *  spend so managing locks for them here would be misleading.
 *
 *  Toggling lock-OFF (unlocking) raises a per-tier confirmation
 *  dialog with appropriately-severe wording -- the most-severe is
 *  for configured masternodes ("the masternode will permanently
 *  fail"), the lightest is for plain "other" locks.
 */
class LockedOutputsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LockedOutputsDialog(QWidget *parent = nullptr);
    ~LockedOutputsDialog();

    void setWalletModel(WalletModel *model);

private:
    Ui::LockedOutputsDialog *ui;
    WalletModel *walletModel;

    /** Row last targeted by a right-click; -1 means no menu open or
     *  cursor was outside any row.  Used by context-menu action
     *  handlers so they act on the right-clicked row, not on
     *  selection. */
    int contextMenuRow;

    /** Repopulate the table from WalletModel::listLockedOutputsWithDetails.
     *  Updates the status footer.  Called on construction, on every
     *  toggle, and on showEvent. */
    void refresh();

    /** Walk the row's lock state, decide which confirmation message
     *  to show (per-tier severity), apply if confirmed. */
    void toggleLockForRow(int row);

    /** Helpers used by row population and context menu */
    QString humanAmount(qint64 satoshis) const;
    QString tierTypeText(int tier, const QString& mnAlias) const;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onCellClicked(int row, int column);
    void onCellDoubleClicked(int row, int column);
    void onCustomContextMenu(const QPoint &pos);
    void onCopyTxId();
    void onCopyAddress();
    void onCopyAmount();
    void onContextLockUnlock();
};

#endif // LOCKEDOUTPUTSDIALOG_H
