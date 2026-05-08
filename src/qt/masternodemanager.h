#ifndef MASTERNODEMANAGER_H
#define MASTERNODEMANAGER_H

#include "compat.h"

#include <QMenu>
#include <QWidget>
#include <QShowEvent>
#include <QTimer>
#include <QItemSelectionModel>

#include "util.h"
#include "types/ccriticalsection.h"
#include "masternodeworker.h"
#include <QThread>
#include <QProgressDialog>

namespace Ui {
    class MasternodeManager;

}
class ClientModel;
class WalletModel;
class QAbstractItemView;
class QItemSelectionModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Masternode Manager page widget */
class MasternodeManager : public QWidget
{
    Q_OBJECT

public:
    explicit MasternodeManager(QWidget *parent = 0);
    ~MasternodeManager();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);

private:
    QMenu* contextMenu;
    /** B2: separate context menu for the user's own configured
     *  masternodes (tableWidget_2).  Holds Lock/Unlock collateral
     *  actions, enabled selectively based on the current row's UTXO
     *  lock state.  Distinct from the "Copy address / Copy pubkey"
     *  contextMenu which is for tableWidgetMasternodes. */
    QMenu* ownContextMenu;
    QAction* lockCollateralAction;
    QAction* unlockCollateralAction;
    /** B2 fix: row index under the cursor when the own-masternodes
     *  context menu was last opened.  Used by the lock/unlock action
     *  handlers so they act on the right-clicked row, not on whatever
     *  is currently selected.  -1 means "no valid row" (handlers
     *  short-circuit). */
    int ownContextMenuRow;
    
public slots:
    void updateNodeList();
    void setButtonsEnabled(bool enabled);
    void onWorkerFinished(QString result);
    void onWorkerError(QString message);
    void updateAdrenalineNode(QString alias, QString addr, QString privkey, QString txHash, QString txIndex, QString status);
    void on_UpdateButton_clicked();
    void copyAddress();
    void copyPubkey();

signals:

private:
    QTimer *timer;
    Ui::MasternodeManager *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    CCriticalSection cs_adrenaline;

    /** B2: refresh the Collateral column for one row.  Reads the
     *  lock state from CWallet via the model and updates the cell. */
    void refreshCollateralCell(int row);

private slots:
    void showContextMenu(const QPoint&);
    /** B2: show lock/unlock context menu over tableWidget_2.  Enables
     *  Lock or Unlock based on whether the selected row's collateral
     *  UTXO is currently locked. */
    void showOwnContextMenu(const QPoint&);
    /** B2: lock the collateral UTXO for the currently-selected row. */
    void lockSelectedCollateral();
    /** B2: unlock the collateral UTXO for the currently-selected row. */
    void unlockSelectedCollateral();
    void on_createButton_clicked();
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_stopButton_clicked();
    void on_stopAllButton_clicked();
    void on_tableWidget_2_itemSelectionChanged();
    void on_tabWidget_currentChanged(int index);
    void on_editButton_clicked();

protected:
    /** Trigger an Update on every show so the My Master Nodes table
     *  populates immediately when the user navigates to the page,
     *  instead of waiting for a tab change.  Without this, the page
     *  lands on the last-selected tab and on_tabWidget_currentChanged
     *  never fires, leaving the Lock column empty until the user
     *  manually clicks Update or switches tabs. */
    void showEvent(QShowEvent *event) override;

};
#endif // MASTERNODEMANAGER_H