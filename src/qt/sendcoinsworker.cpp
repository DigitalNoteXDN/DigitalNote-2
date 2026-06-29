// Copyright (c) 2024 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT

#include "sendcoinsworker.h"
#include "walletmodel.h"
#include "walletmodeltransaction.h"
#include "ccoincontrol.h"
#include "cwallettx.h"         // CWalletTx

SendCoinsWorker::SendCoinsWorker(WalletModel *model,
                                 QList<SendCoinsRecipient> recipients,
                                 CCoinControl *coinControl,
                                 QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_recipients(std::move(recipients))
    , m_coinControl(coinControl)
{
}

void SendCoinsWorker::run()
{
    try {
        WalletModel::UnlockContext ctx(m_model->requestUnlock());
        if (!ctx.isValid()) {
            emit error(tr("Wallet could not be unlocked."));
            return;
        }

        // Step 1: prepare (select inputs, compute fee)
        WalletModelTransaction currentTransaction(m_recipients);
        WalletModel::SendCoinsReturn prepareStatus =
            m_model->prepareTransaction(currentTransaction, m_coinControl);

        if (prepareStatus.status != WalletModel::OK) {
            emit finished(prepareStatus, QString());
            return;
        }

        // Step 2: broadcast
        WalletModel::SendCoinsReturn sendStatus =
            m_model->sendCoins(currentTransaction, m_coinControl);

        QString txid;
        if (sendStatus.status == WalletModel::OK) {
            CWalletTx *wtx = currentTransaction.getTransaction();
            if (wtx)
                txid = QString::fromStdString(wtx->GetHash().GetHex());
        }

        emit finished(sendStatus, txid);

    } catch (const std::exception &e) {
        emit error(QString::fromStdString(e.what()));
    } catch (...) {
        emit error(QStringLiteral("Unknown error during send"));
    }
}
