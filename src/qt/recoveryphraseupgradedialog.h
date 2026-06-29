// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// recoveryphraseupgradedialog.h
// One-shot prompt offered to users whose wallet was created before D2
// shipped (i.e. wallets that lack CMasterKey[2]).  Triggered from
// BitcoinGUI in response to WalletModel::recoveryPhraseUpgradeAvailable.
//
// Three user choices:
//   "Set up now"     -- adds CMasterKey[2] to the wallet, then opens
//                       SeedPhraseDialog in FirstTimeAutoReveal mode
//                       to display the new phrase
//   "Maybe later"    -- close, prompt again on next unlock
//   "Don't ask again"-- persistently record the decline; never prompt
//                       again for this wallet.  Tells the user where
//                       to set it up later (Settings > Recovery Phrase).

#pragma once

#include <QDialog>
class WalletModel;

class RecoveryPhraseUpgradeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RecoveryPhraseUpgradeDialog(WalletModel *model, QWidget *parent = nullptr);
    ~RecoveryPhraseUpgradeDialog() override = default;

private slots:
    void onSetUpNow();
    void onMaybeLater();
    void onDontAskAgain();

private:
    WalletModel *m_model;
};
