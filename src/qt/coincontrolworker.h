// Copyright (c) 2024 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// coincontrolworker.h -- off-thread UTXO enumeration for CoinControlDialog

#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "coutput.h"      // COutput
#include "ccoincontrol.h" // CCoinControl

class CWallet;

class CoinControlWorker : public QObject
{
    Q_OBJECT

public:
    explicit CoinControlWorker(CWallet *wallet,
                               CCoinControl *coinControl,
                               QObject *parent = nullptr);

public slots:
    void run();

signals:
    void finished(QList<COutput> utxos);
    void error(QString message);

private:
    CWallet      *m_wallet;
    CCoinControl *m_coinControl;
};
