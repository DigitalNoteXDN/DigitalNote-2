// Copyright (c) 2024 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT

#include "masternodeworker.h"

#include "cactivemasternode.h"
#include "mnengine_extern.h"
#include "init.h"
#include "cwallet.h"
#include "wallet.h"
#include "init.h"
#include "cwallet.h"

#include <boost/lexical_cast.hpp>

MasternodeWorker::MasternodeWorker(Operation op,
                                   std::vector<CMasternodeConfigEntry> entries,
                                   QObject *parent)
    : QObject(parent)
    , m_op(op)
    , m_entries(std::move(entries))
{
}

void MasternodeWorker::run()
{
    try {
        int total = static_cast<int>(m_entries.size());
        int successful = 0;
        int fail = 0;
        std::string statusObj;

        switch (m_op) {
        case StartSelected:
        case StartAll:
        {
            int idx = 0;
            for (const CMasternodeConfigEntry& mne : m_entries) {
                idx++;
                emit progress(idx, total, QString::fromStdString(mne.getAlias()));

                std::string errorMessage;
                std::string strDonateAddress;
                std::string strDonationPercentage;

                bool result = activeMasternode.Register(
                    mne.getIp(), mne.getPrivKey(),
                    mne.getTxHash(), mne.getOutputIndex(),
                    strDonateAddress, strDonationPercentage, errorMessage);

                if (result) {
                    successful++;
                } else {
                    fail++;
                    statusObj += "\nFailed to start " + mne.getAlias() + ". Error: " + errorMessage;
                }
            }

            // v2.0.0.8 UAT-6b: do NOT lock the wallet after start.
            // Previously this unconditionally re-locked, undoing the
            // staking-only unlock the user just performed via the
            // AskPassphraseDialog.  That broke local MN auto-recovery
            // because ManageStatus needs an unlocked wallet to handle
            // re-registration after transient network drops.  Honour
            // the user's chosen lock state instead.

            std::string returnObj = "Successfully started " +
                boost::lexical_cast<std::string>(successful) +
                " masternode(s), failed to start " +
                boost::lexical_cast<std::string>(fail) +
                ", total " + boost::lexical_cast<std::string>(total);

            if (fail > 0)
                returnObj += statusObj;

            emit finished(QString::fromStdString(returnObj));
            break;
        }

        case StopSelected:
        case StopAll:
        {
            int idx = 0;
            for (const CMasternodeConfigEntry& mne : m_entries) {
                idx++;
                emit progress(idx, total, QString::fromStdString(mne.getAlias()));

                std::string errorMessage;
                // v2.0.0.8 PB-13 fix: pass full collateral identity
                // (txhash + vout) so the correct MN is targeted.  Previous
                // call used the (ip, privkey, error) overload which fell
                // back to possibleCoins[0] in GetMasterNodeVin -- always
                // stopping the same MN (whichever has the first 2M UTXO)
                // regardless of which alias was selected in the GUI.
                bool result = activeMasternode.StopMasterNode(
                    mne.getIp(), mne.getPrivKey(),
                    mne.getTxHash(), mne.getOutputIndex(),
                    errorMessage);

                if (result) {
                    successful++;
                } else {
                    fail++;
                    statusObj += "\nFailed to stop " + mne.getAlias() + ". Error: " + errorMessage;
                }
            }

            // v2.0.0.8 UAT-6b: do NOT lock the wallet after stop.
            // (See StartSelected branch above for full rationale.)

            std::string returnObj = "Successfully stopped " +
                boost::lexical_cast<std::string>(successful) +
                " masternode(s), failed " +
                boost::lexical_cast<std::string>(fail) +
                ", total " + boost::lexical_cast<std::string>(total);

            if (fail > 0)
                returnObj += statusObj;

            emit finished(QString::fromStdString(returnObj));
            break;
        }

        case Update:
            emit finished(QString());
            break;
        }

    } catch (const std::exception& e) {
        emit error(QString::fromStdString(e.what()));
    } catch (...) {
        emit error(QStringLiteral("Unknown error during masternode operation"));
    }
}
