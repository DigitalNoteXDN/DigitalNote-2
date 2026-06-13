// Copyright (c) 2024-2026 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// watchonlyworker.h -- off-thread watch-only address removal
// Removes one or more watch-only entries from the wallet without
// blocking the GUI thread.  Each call is fast (just an in-memory set
// erase plus a BDB record erase), but bulk operations and BDB write
// latency justify keeping it off the UI thread for responsiveness.

#pragma once

#include <vector>

#include <QObject>
#include <QString>

#include "cscript.h"

class WalletModel;

class WatchOnlyWorker : public QObject
{
    Q_OBJECT

public:
    explicit WatchOnlyWorker(WalletModel *model,
                             const std::vector<CScript> &scripts,
                             QObject *parent = nullptr);

public slots:
    void run();

signals:
    void progress(int current, int total, QString label);
    void finished(int succeeded, int failed, QString errorSummary);

private:
    WalletModel              *m_model;
    std::vector<CScript>      m_scripts;
};
