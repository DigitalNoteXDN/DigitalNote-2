// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// seedphrasedialog.h
// Qt dialog/tab that shows (and optionally verifies) the BIP39 seed phrase
// for the current wallet.  Designed to be embedded as a tab in the main
// wallet window OR used as a standalone modal dialog.
//
// Two display modes
// -----------------
//   * Normal -- requires the wallet to be unlocked (password prompt) before
//     the phrase is shown.  Used for the "Show recovery phrase" menu item.
//
//   * FirstTimeAutoReveal -- caller already has the phrase in hand (because
//     they just generated it during initial encryption).  The dialog opens
//     with the phrase visible immediately, no password prompt, and the
//     "Replace phrase…" rotation button is hidden (rotating a brand-new
//     phrase makes no sense).
//
// Security design
// ---------------
//  * The mnemonic is never stored in a QLabel's text — it is written directly
//    into the styled word grid and cleared on close via clearMnemonic().
//  * In Normal mode, the "Reveal" button requires the wallet to be unlocked
//    first, and a 10-second countdown must complete before display.
//  * In FirstTimeAutoReveal mode the phrase is shown immediately; the user
//    already authenticated to set the password moments ago.
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
    enum class Mode {
        Normal,                  // password-prompted reveal flow
        FirstTimeAutoReveal      // phrase passed in, shown immediately, rotation hidden
    };

    /** Normal-mode constructor: wallet must be unlocked (or unlockable)
     *  before the phrase can be revealed. */
    explicit SeedPhraseDialog(WalletModel *model, QWidget *parent = nullptr);

    /** FirstTimeAutoReveal-mode constructor: caller already has the
     *  mnemonic in hand and just wants it displayed prominently.  The
     *  preknownMnemonic is shown immediately, no password prompt, and
     *  the rotation control is hidden. */
    SeedPhraseDialog(WalletModel *model,
                     QWidget *parent,
                     Mode mode,
                     const QString& preknownMnemonic);

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
    void onRotateClicked();

private:
    void setupUi();
    void applyModeAdjustments();   // hide/disable widgets based on m_mode
    void setMnemonicVisible(bool visible);
    void startCountdown(int seconds = 10);
    void showMnemonic(const QString& words);
    bool ensureUnlocked();

    Ui::SeedPhraseDialog *ui{nullptr};
    WalletModel          *m_model{nullptr};
    Mode                  m_mode{Mode::Normal};

    QTimer  m_countdownTimer;
    QTimer  m_clipboardTimer;
    int     m_countdownSecondsLeft{0};

    BIP39Wallet::WordCount m_wordCount{BIP39Wallet::WordCount::Words24}; // Fixed at 24 words

    // Holds the mnemonic in Qt-managed memory; cleared on close
    QString m_currentMnemonic;
};
