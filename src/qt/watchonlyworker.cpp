// Copyright (c) 2024-2026 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT

#include "watchonlyworker.h"
#include "walletmodel.h"

#include <QString>

WatchOnlyWorker::WatchOnlyWorker(WalletModel *model,
                                 const std::vector<CScript> &scripts,
                                 QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_scripts(scripts)
{
}

void WatchOnlyWorker::run()
{
    int total = static_cast<int>(m_scripts.size());
    int succeeded = 0;
    int failed = 0;

    if (total == 0)
    {
        emit finished(0, 0, QString());
        return;
    }

    // Total work units = total scripts * 100 (each script is 100 sub-units).
    // Allows the GUI progress bar to move smoothly inside a single script's
    // removal, which is the bulk of the time when a watch address has
    // thousands of historical transactions.
    const int totalUnits = total * 100;

    for (int i = 0; i < total; ++i)
    {
        // Report start-of-script progress (covers the case where the
        // callback below never fires, e.g. for a script with no orphan
        // sweep needed).
        emit progress(i * 100, totalUnits,
                      tr("Removing watch-only address %1 of %2...").arg(i + 1).arg(total));

        // Per-script progress callback: turns (percent, label) within
        // this script into (currentUnits, totalUnits) for the dialog.
        // The lambda runs on THIS thread (worker thread) since the
        // callback is invoked synchronously by CWallet::RemoveWatchOnly
        // under the wallet lock.  emit progress(...) on a Qt::QueuedConnection
        // signal is thread-safe and marshals to the GUI thread.
        const int scriptIdx = i;
        WalletModel::RemoveWatchOnlyProgressFn cb =
            [this, scriptIdx, total, totalUnits](int percent, const std::string& label) {
                int current = scriptIdx * 100 + percent;
                QString qlabel = QString::fromStdString(label);
                emit progress(current, totalUnits,
                              tr("Removing watch-only address %1 of %2 — %3")
                                  .arg(scriptIdx + 1).arg(total).arg(qlabel));
            };

        // removeWatchOnly is a Qt-side bridge that takes the wallet lock
        // and calls CWallet::RemoveWatchOnly under it.  Errors here
        // mean the script wasn't actually being watched (race with
        // another remover), or BDB write failure.  Either way, count
        // and continue -- one bad entry shouldn't abort the rest.
        if (m_model->removeWatchOnly(m_scripts[i], cb))
        {
            ++succeeded;
        }
        else
        {
            ++failed;
        }
    }

    emit progress(totalUnits, totalUnits, tr("Done."));

    QString errorSummary;
    if (failed > 0)
    {
        errorSummary = tr("%1 address(es) could not be removed.").arg(failed);
    }

    emit finished(succeeded, failed, errorSummary);
}
