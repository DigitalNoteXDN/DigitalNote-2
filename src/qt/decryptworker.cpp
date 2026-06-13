// Copyright (c) 2024-2025 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// decryptworker.cpp -- off-thread mnemonic master key registration
// Adds a second master key derived from the mnemonic so both the
// wallet password AND the 24-word recovery phrase unlock the wallet.

#include "decryptworker.h"
#include "walletmodel.h"

#include <openssl/crypto.h>

DecryptWorker::DecryptWorker(WalletModel *model,
                             const SecureString &passphrase,
                             QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_passphrase(passphrase)
{
}

void DecryptWorker::run()
{
    emit progress(0, 1, tr("Adding recovery phrase key to wallet..."));

    // D2: addMnemonicMasterKey takes no arguments now -- it derives the
    // mnemonic from the in-memory vMasterKey rather than from the password.
    // The caller is responsible for ensuring the wallet is unlocked before
    // starting this worker.  We retain m_passphrase as a (now-unused)
    // member only to avoid breaking the existing constructor signature
    // and to keep the caller's cleanse-on-completion contract working.
    (void)m_passphrase;

    if (!m_model->addMnemonicMasterKey()) {
        OPENSSL_cleanse(const_cast<char*>(m_passphrase.data()), m_passphrase.size());
        emit finished(false, tr("Failed to add recovery phrase key. "
                                "Please restart the wallet and try again."));
        return;
    }

    emit progress(1, 1, tr("Done."));
    OPENSSL_cleanse(const_cast<char*>(m_passphrase.data()), m_passphrase.size());
    emit finished(true, QString());
}
