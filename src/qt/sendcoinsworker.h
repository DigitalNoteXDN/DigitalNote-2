// Copyright (c) 2024 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// sendcoinsworker.h -- off-thread transaction building and broadcast

#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "walletmodel.h"             // WalletModel::SendCoinsReturn
#include "walletmodeltransaction.h"  // WalletModelTransaction

class CCoinControl;
class WalletModel;

class SendCoinsWorker : public QObject
{
    Q_OBJECT

public:
    explicit SendCoinsWorker(WalletModel *model,
                             QList<SendCoinsRecipient> recipients,
                             CCoinControl *coinControl,
                             QObject *parent = nullptr);

public slots:
    void run();

signals:
    void finished(WalletModel::SendCoinsReturn result, QString txid);
    void error(QString message);

private:
    WalletModel               *m_model;
    QList<SendCoinsRecipient>  m_recipients;
    CCoinControl              *m_coinControl;
};
