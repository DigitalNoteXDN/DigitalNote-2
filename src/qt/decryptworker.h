// Copyright (c) 2024-2025 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// decryptworker.h -- off-thread mnemonic master key registration

#pragma once

#include <QObject>
#include <QString>

#include "allocators/securestring.h"

class WalletModel;

class DecryptWorker : public QObject
{
    Q_OBJECT

public:
    explicit DecryptWorker(WalletModel *model,
                           const SecureString &passphrase,
                           QObject *parent = nullptr);

public slots:
    void run();

signals:
    void progress(int current, int total, QString label);
    void finished(bool success, QString errorMessage);

private:
    WalletModel  *m_model;
    SecureString  m_passphrase;
};
