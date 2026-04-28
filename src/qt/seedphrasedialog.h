// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// seedphrasedialog.h
// Qt dialog/tab that shows (and optionally verifies) the BIP39 seed phrase
// for the current wallet.  Designed to be embedded as a tab in the main
// wallet window OR used as a standalone modal dialog.
//
// Security design
// ---------------
//  * The mnemonic is never stored in a QLabel's text — it is written directly
//    into a QTextEdit and cleared on close via clearMnemonic().
//  * The "Reveal" button requires the wallet to be unlocked first.
//  * A mandatory 10-second countdown must complete before the mnemonic is
//    shown, matching best-practice UX for seed phrase display.
//  * Copy-to-clipboard is available but accompanied by a clipboard-clear
//    timer (30 seconds).

#pragma once

#include <QDialog>
#include <QTimer>
#include <QString>

#include <bip39/bip39_wallet.h>   // BIP39Wallet::WordCount, Result

namespace Ui { class SeedPhraseDialog; }

class WalletModel;
class QTextEdit;
class QPushButton;
class QLabel;

class SeedPhraseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SeedPhraseDialog(WalletModel *model, QWidget *parent = nullptr);
    ~SeedPhraseDialog() override;

    /** Securely clears the displayed mnemonic from the widget and memory. */
    void clearMnemonic();

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent  *event) override;

private slots:
    void onRevealClicked();
    void onCopyClicked();
    void onCountdownTick();
    void onClipboardClearTick();
    void onVerifyClicked();

private:
    void setupUi();
    void setMnemonicVisible(bool visible);
    void startCountdown(int seconds = 10);
    void showMnemonic(const QString& words);
    bool ensureUnlocked();

    Ui::SeedPhraseDialog *ui{nullptr};
    WalletModel          *m_model{nullptr};

    QTimer  m_countdownTimer;
    QTimer  m_clipboardTimer;
    int     m_countdownSecondsLeft{0};

    BIP39Wallet::WordCount m_wordCount{BIP39Wallet::WordCount::Words24}; // Fixed at 24 words

    // Holds the mnemonic in Qt-managed memory; cleared on close
    QString m_currentMnemonic;
};
