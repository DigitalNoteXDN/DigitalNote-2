// Copyright (c) 2024 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// masternodeworker.h -- off-thread masternode start/stop operations

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>

#include "masternodeconfig.h"
#include "cmasternodeconfigentry.h"

class MasternodeWorker : public QObject
{
    Q_OBJECT

public:
    enum Operation {
        StartSelected,
        StartAll,
        StopSelected,
        StopAll,
        Update
    };

    explicit MasternodeWorker(Operation op,
                              std::vector<CMasternodeConfigEntry> entries,
                              QObject *parent = nullptr);

public slots:
    void run();

signals:
    void finished(QString result);
    void error(QString message);
    void progress(int current, int total, QString alias);

private:
    Operation m_op;
    std::vector<CMasternodeConfigEntry> m_entries;
};
