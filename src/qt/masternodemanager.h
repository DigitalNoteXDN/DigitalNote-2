#ifndef MASTERNODEMANAGER_H
#define MASTERNODEMANAGER_H

#include "compat.h"

#include <QMenu>
#include <QWidget>
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

};
#endif // MASTERNODEMANAGER_H