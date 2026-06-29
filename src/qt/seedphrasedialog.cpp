// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT

#include "rotatephrasedialog.h"
#include "seedphrasedialog.h"
#include "decryptworker.h"
#include <QThread>
#include <QProgressDialog>
#include <QApplication>
#include "walletmodel.h"
#include "guiutil.h"
#include <bip39/bip39_wallet.h>
#include <openssl/crypto.h>

#include <QCloseEvent>
#include <QClipboard>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QCheckBox>
#include <QFrame>
#include <QFont>
#include <QScrollArea>
#include <QGridLayout>
#include <QSizePolicy>

// ── Constructor / Destructor ────────────────────────────────────────────────

SeedPhraseDialog::SeedPhraseDialog(WalletModel *model, QWidget *parent)
    : QDialog(parent)
    , m_model(model)
    , m_mode(Mode::Normal)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(tr("Wallet Seed Phrase (BIP39)"));
    setMinimumSize(680, 520);
    setModal(true);
    setAttribute(Qt::WA_DeleteOnClose, false);  // caller owns lifetime

    setupUi();
    applyModeAdjustments();

    connect(&m_countdownTimer, &QTimer::timeout,
            this,              &SeedPhraseDialog::onCountdownTick);
    connect(&m_clipboardTimer, &QTimer::timeout,
            this,              &SeedPhraseDialog::onClipboardClearTick);
    m_clipboardTimer.setSingleShot(true);
}

SeedPhraseDialog::SeedPhraseDialog(WalletModel *model,
                                   QWidget *parent,
                                   Mode mode,
                                   const QString& preknownMnemonic)
    : QDialog(parent)
    , m_model(model)
    , m_mode(mode)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(m_mode == Mode::FirstTimeAutoReveal
                   ? tr("Your 24-Word Recovery Phrase")
                   : tr("Wallet Seed Phrase (BIP39)"));
    setMinimumSize(680, 520);
    setModal(true);
    setAttribute(Qt::WA_DeleteOnClose, false);

    setupUi();
    applyModeAdjustments();

    connect(&m_countdownTimer, &QTimer::timeout,
            this,              &SeedPhraseDialog::onCountdownTick);
    connect(&m_clipboardTimer, &QTimer::timeout,
            this,              &SeedPhraseDialog::onClipboardClearTick);
    m_clipboardTimer.setSingleShot(true);

    // Auto-reveal the supplied phrase immediately.
    if (m_mode == Mode::FirstTimeAutoReveal && !preknownMnemonic.isEmpty()) {
        m_currentMnemonic = preknownMnemonic;
        showMnemonic(m_currentMnemonic);

        // In auto-reveal we DO want Copy and Verify enabled right away
        // (the user just generated this; they need to either save it now
        // or verify they wrote it down correctly).
        if (auto *copyBtn   = findChild<QPushButton*>("copyBtn"))   copyBtn->setEnabled(true);
        if (auto *verifyBtn = findChild<QPushButton*>("verifyBtn")) verifyBtn->setEnabled(true);
    }
}

SeedPhraseDialog::~SeedPhraseDialog()
{
    clearMnemonic();
}

// ── UI setup (programmatic — no .ui file dependency) ────────────────────────

void SeedPhraseDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(16, 16, 16, 16);

    // ── Warning banner ──────────────────────────────────────────────────────
    auto *warnFrame = new QFrame;
    warnFrame->setFrameShape(QFrame::StyledPanel);
    warnFrame->setStyleSheet(
        "QFrame { background:#fff3cd; border:1px solid #ffc107; border-radius:4px; }"
        "QLabel { background:transparent; color:#856404; font-size:9pt; }");
    auto *warnLayout = new QHBoxLayout(warnFrame);
    auto *warnIcon  = new QLabel("⚠");
    warnIcon->setStyleSheet("font-size:20pt; color:#856404;");
    auto *warnText  = new QLabel(
        tr("<b>Keep your seed phrase private.</b><br>"
           "Anyone with these words can access all your funds. "
           "Write them down on paper and store them securely offline. "
           "Never enter them on a website or share them digitally."));
    warnText->setWordWrap(true);
    warnLayout->addWidget(warnIcon);
    warnLayout->addWidget(warnText, 1);
    root->addWidget(warnFrame);

    // ── Seed phrase display area ─────────────────────────────────────────────
    auto *seedGroup = new QGroupBox(tr("Your Seed Phrase"));
    auto *seedLayout = new QVBoxLayout(seedGroup);

    // Grid to display each word individually — cleaner than a text blob
    auto *wordGrid = new QWidget;
    wordGrid->setObjectName("wordGrid");
    wordGrid->setLayout(new QGridLayout);
    wordGrid->hide();
    seedLayout->addWidget(wordGrid);

    // Placeholder shown before reveal
    auto *placeholderLabel = new QLabel(tr("Your recovery phrase is a 24-word backup of your wallet password.\n\n"
           "If you forget your password, enter these words in Settings \u2192 Unlock Wallet \u2192 Forgot password?\n\n"
           "Note: This backs up your password only, not your keys. Keep your wallet.dat file backed up separately."));
    placeholderLabel->setObjectName("placeholderLabel");
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setStyleSheet("color:#888; font-size:10pt;");
    seedLayout->addWidget(placeholderLabel);

    root->addWidget(seedGroup, 1);

    // ── Countdown / reveal row ───────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;

    auto *countdownLabel = new QLabel;
    countdownLabel->setObjectName("countdownLabel");
    countdownLabel->setStyleSheet("color:#555; font-size:9pt;");
    countdownLabel->hide();
    btnRow->addWidget(countdownLabel);

    btnRow->addStretch();

    auto *copyBtn = new QPushButton(tr("Copy to Clipboard"));
    copyBtn->setObjectName("copyBtn");
    copyBtn->setEnabled(false);
    connect(copyBtn, &QPushButton::clicked, this, &SeedPhraseDialog::onCopyClicked);
    btnRow->addWidget(copyBtn);

    auto *verifyBtn = new QPushButton(tr("Verify Phrase…"));
    verifyBtn->setObjectName("verifyBtn");
    verifyBtn->setEnabled(false);
    verifyBtn->setToolTip(tr("Enter your seed phrase back to confirm you recorded it correctly"));
    connect(verifyBtn, &QPushButton::clicked, this, &SeedPhraseDialog::onVerifyClicked);
    btnRow->addWidget(verifyBtn);

    auto *revealBtn = new QPushButton(tr("Reveal Seed Phrase"));
    revealBtn->setObjectName("revealBtn");
    revealBtn->setStyleSheet(
        "QPushButton { background:#d9534f; color:white; font-weight:bold; "
        "padding:6px 14px; border-radius:4px; border:none; }"
        "QPushButton:hover { background:#c9302c; }"
        "QPushButton:disabled { background:#aaa; }");
    connect(revealBtn, &QPushButton::clicked, this, &SeedPhraseDialog::onRevealClicked);
    btnRow->addWidget(revealBtn);

    root->addLayout(btnRow);

    // ── Close + advanced rotation ────────────────────────────────────────────
    // "Replace phrase…" launches RotatePhraseDialog (compromised phrase
    // remediation).  Buried here rather than promoted to a top-level menu
    // item -- this is a destructive security operation, not a casual setting.
    auto *rotateBtn = new QPushButton(tr("Replace phrase…"));
    rotateBtn->setObjectName("rotateBtn");
    rotateBtn->setToolTip(tr(
        "Generate a new recovery phrase to replace one that may have been "
        "compromised.  Advanced; rarely needed."));
    rotateBtn->setStyleSheet(
        "QPushButton { color:#a94442; font-weight:bold; padding:6px 14px; "
        "background:transparent; border:1px solid #a94442; border-radius:4px; }"
        "QPushButton:hover { background:#f2dede; }");
    connect(rotateBtn, &QPushButton::clicked,
            this, &SeedPhraseDialog::onRotateClicked);

    auto *closeBtn = new QPushButton(tr("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *closeRow = new QHBoxLayout;
    closeRow->addWidget(rotateBtn);   // bottom-left, distinct red border
    closeRow->addStretch();
    closeRow->addWidget(closeBtn);    // bottom-right, primary action
    root->addLayout(closeRow);
}

// ── Mode-driven UI adjustments ───────────────────────────────────────────────
//
// Called after setupUi() in both constructors.  The Normal-mode UI is the
// default state already built by setupUi(); FirstTimeAutoReveal-mode hides
// controls that don't make sense for a freshly-generated phrase:
//
//   * Reveal button  -- the phrase is shown immediately, no need to reveal
//   * Rotate button  -- rotating a brand-new phrase makes no sense
//   * Placeholder text -- the wordGrid will be populated immediately so
//                         the "your phrase will appear here" text is wrong
//   * The warning banner is replaced with text matching the moment
//     ("write this down NOW") rather than the generic "keep it private".

void SeedPhraseDialog::applyModeAdjustments()
{
    if (m_mode != Mode::FirstTimeAutoReveal)
        return;

    if (auto *w = findChild<QPushButton*>("revealBtn"))     w->hide();
    if (auto *w = findChild<QPushButton*>("rotateBtn"))     w->hide();
    if (auto *w = findChild<QLabel*>("placeholderLabel"))   w->hide();

    // Update the warning banner text.  We find it by walking children
    // since it doesn't have an explicit objectName; the banner is the
    // only QLabel whose styleSheet sets the warning yellow color, but
    // the simplest reliable way is to find the GroupBox title and just
    // re-title it with first-time-specific copy.
    if (auto *grp = findChild<QGroupBox*>()) {
        grp->setTitle(tr("Write These 24 Words Down Now"));
    }
}

// ── Slot implementations ─────────────────────────────────────────────────────

bool SeedPhraseDialog::ensureUnlocked()
{
    if (!m_model) return false;
    if (m_model->getEncryptionStatus() == WalletModel::Locked) {
        WalletModel::UnlockContext ctx(m_model->requestUnlock());
        return ctx.isValid();
    }
    return true;
}

void SeedPhraseDialog::onRevealClicked()
{
    if (!m_model) return;

    // Ensure wallet is unlocked before proceeding.  The UnlockContext is
    // an RAII handle: while `ctx` is alive, the wallet stays unlocked.
    // When `ctx` is destroyed at end-of-scope (this function returning)
    // the wallet returns to its prior lock state.
    //
    // We need the wallet unlocked for getCurrentMnemonic() to work, so
    // we fetch the phrase WHILE the context is alive (a few lines below)
    // and stash it in m_currentMnemonic.  After this function returns,
    // the wallet relocks but we already have what we need.
    //
    // requestUnlock() is a no-op (returns a valid no-relock context) if
    // the wallet is already unlocked, so calling it unconditionally is
    // correct.
    WalletModel::UnlockContext ctx = m_model->requestUnlock();
    if (!ctx.isValid()) return;

    // Old wallet - add mnemonic master key if not already present
    // This allows both password AND recovery phrase to unlock the wallet
    if (m_model->getEncryptionStatus() != WalletModel::Unencrypted &&
        !m_model->hasRecoveryPhraseSupport())
    {
        int ret = QMessageBox::information(this,
            tr("One-time wallet upgrade required"),
            tr("Your wallet was encrypted with an older version of DigitalNote\n"
               "and does not yet have a recovery phrase.\n\n"
               "Click OK and enter your password to complete this one-time upgrade.\n"
               "Your wallet stays encrypted throughout - no risk to your funds."),
            QMessageBox::Ok | QMessageBox::Cancel);

        if (ret != QMessageBox::Ok)
            return;

        bool ok = false;
        QString passStr = QInputDialog::getText(
            this,
            tr("Recovery Phrase"),
            tr("Enter your wallet password to upgrade:"),
            QLineEdit::Password, QString(), &ok);

        if (!ok || passStr.isEmpty())
            return;

        SecureString upgradePass;
        std::string passStd = passStr.toStdString();
        upgradePass.assign(passStd.c_str(), passStd.size());
        OPENSSL_cleanse(const_cast<char*>(passStd.data()), passStd.size());

        // Verify password first
        if (!m_model->verifyPassphrase(upgradePass)) {
            OPENSSL_cleanse(const_cast<char*>(upgradePass.data()), upgradePass.size());
            QMessageBox::critical(this, tr("Recovery Phrase"),
                tr("The password you entered is incorrect. Please try again."));
            return;
        }

        // Unlock wallet so AddMnemonicMasterKey can access vMasterKey
        bool wasLocked = (m_model->getEncryptionStatus() == WalletModel::Locked);
        if (wasLocked) {
            if (!m_model->setWalletLocked(false, upgradePass)) {
                OPENSSL_cleanse(const_cast<char*>(upgradePass.data()), upgradePass.size());
                QMessageBox::critical(this, tr("Recovery Phrase"),
                    tr("Could not unlock wallet. Please try again."));
                return;
            }
        }

        // Add mnemonic as second master key - wallet stays encrypted with existing password
        QProgressDialog progress(
            tr("Adding recovery phrase key..."),
            QString(), 0, 1, this);
        progress.setWindowTitle(tr("Recovery Phrase - Upgrading Wallet"));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumWidth(420);
        progress.setWindowFlags(progress.windowFlags() & ~Qt::WindowContextHelpButtonHint);
        progress.setMinimumDuration(0);
        progress.setValue(0);
        progress.show();
        QApplication::processEvents();

        QThread *thread = new QThread(this);
        DecryptWorker *worker = new DecryptWorker(m_model, upgradePass);
        OPENSSL_cleanse(const_cast<char*>(upgradePass.data()), upgradePass.size());
        worker->moveToThread(thread);

        bool upgradeSuccess = false;
        QString upgradeError;

        connect(thread, &QThread::started, worker, &DecryptWorker::run);
        connect(worker, &DecryptWorker::progress, [&](int cur, int total, QString label) {
            progress.setRange(0, total);
            progress.setValue(cur);
            progress.setLabelText(label);
            QApplication::processEvents();
        });
        connect(worker, &DecryptWorker::finished, [&](bool ok, QString err) {
            upgradeSuccess = ok;
            upgradeError = err;
            thread->quit();
        });
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        thread->start();
        while (thread->isRunning()) {
            QApplication::processEvents(QEventLoop::AllEvents, 100);
        }
        progress.close();

        // Re-lock if wallet was locked before
        if (wasLocked)
            m_model->setWalletLocked(true);

        if (!upgradeSuccess) {
            QMessageBox::critical(this, tr("Upgrade failed"), upgradeError);
            return;
        }

        QMessageBox::information(this, tr("Wallet upgraded"),
            tr("Your wallet has been upgraded successfully.\n"
               "Both your password and recovery phrase now unlock your wallet."));
    }

    // Wallet must be encrypted to have a recovery phrase.
    if (m_model->getEncryptionStatus() == WalletModel::Unencrypted) {
        QMessageBox::information(this, tr("Recovery Phrase Unavailable"),
            tr("Your wallet is not encrypted.<br><br>"
               "A recovery phrase is only available for encrypted wallets.<br><br>"
               "Go to <b>Settings \u2192 Encrypt Wallet</b> to encrypt your wallet "
               "and receive a 24-word recovery phrase."));
        return;
    }

    // Fetch the mnemonic NOW, while the wallet is still unlocked (the
    // UnlockContext above is keeping it that way).  After this function
    // returns, the wallet relocks -- but we'll have stashed the phrase
    // in m_currentMnemonic and the countdown can display it from there
    // without needing the wallet unlocked again.
    {
        SecureString mnemonic;
        if (!m_model->getCurrentMnemonic(mnemonic)) {
            QMessageBox::critical(this, tr("Recovery Phrase"),
                tr("Could not retrieve the recovery phrase.  Please make "
                   "sure the wallet is unlocked and try again."));
            return;
        }
        m_currentMnemonic = QString::fromStdString(
            std::string(mnemonic.begin(), mnemonic.end()));
        OPENSSL_cleanse(const_cast<char*>(mnemonic.data()), mnemonic.size());
    }

    // Start countdown — phrase will be displayed when it elapses.
    auto *revealBtn = findChild<QPushButton*>("revealBtn");
    if (revealBtn) revealBtn->setEnabled(false);

    startCountdown(10);
}

void SeedPhraseDialog::startCountdown(int seconds)
{
    m_countdownSecondsLeft = seconds;

    auto *label = findChild<QLabel*>("countdownLabel");
    if (label) {
        label->setText(tr("Revealing in %1 seconds…").arg(m_countdownSecondsLeft));
        label->show();
    }

    m_countdownTimer.start(1000);
}

void SeedPhraseDialog::onCountdownTick()
{
    --m_countdownSecondsLeft;
    auto *label = findChild<QLabel*>("countdownLabel");

    if (m_countdownSecondsLeft > 0) {
        if (label)
            label->setText(tr("Revealing in %1 seconds…").arg(m_countdownSecondsLeft));
        return;
    }

    m_countdownTimer.stop();
    if (label) label->hide();

    // The wallet must be encrypted — unencrypted wallets have no recovery
    // phrase to display.
    if (m_model->getEncryptionStatus() == WalletModel::Unencrypted) {
        QMessageBox::information(this, tr("Recovery Phrase Unavailable"),
            tr("Your wallet is not encrypted.<br><br>"
               "A recovery phrase is only available for encrypted wallets.<br><br>"
               "Go to <b>Settings \u2192 Encrypt Wallet</b> to encrypt your wallet "
               "and receive a 24-word recovery phrase."));
        auto *revealBtn = findChild<QPushButton*>("revealBtn");
        if (revealBtn) revealBtn->setEnabled(true);
        return;
    }

    // The mnemonic was already fetched and stashed in m_currentMnemonic
    // by onRevealClicked, while the UnlockContext was alive.  By now the
    // wallet may have re-locked (when onRevealClicked returned), so we
    // can't fetch fresh -- but we don't need to, we already have it.
    if (m_currentMnemonic.isEmpty()) {
        QMessageBox::critical(this, tr("Recovery Phrase"),
            tr("Could not retrieve the recovery phrase.  Please try again."));
        auto *revealBtn = findChild<QPushButton*>("revealBtn");
        if (revealBtn) revealBtn->setEnabled(true);
        return;
    }

    showMnemonic(m_currentMnemonic);
}

void SeedPhraseDialog::showMnemonic(const QString& words)
{
    QStringList wordList = words.split(' ', Qt::SkipEmptyParts);

    // Rebuild the word grid
    auto *gridWidget = findChild<QWidget*>("wordGrid");
    auto *placeholder = findChild<QLabel*>("placeholderLabel");
    if (!gridWidget) return;

    // Clear old grid
    QLayout *old = gridWidget->layout();
    QLayoutItem *item;
    while (old && (item = old->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    delete old;

    auto *grid = new QGridLayout(gridWidget);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    const int cols = (wordList.size() <= 12) ? 3 : 4;
    for (int i = 0; i < wordList.size(); ++i) {
        int row = i / cols;
        int col = i % cols;

        auto *cell = new QFrame;
        cell->setFrameShape(QFrame::StyledPanel);
        cell->setStyleSheet(
            "QFrame { background:#f0f4f8; border:1px solid #c5cdd6; border-radius:4px; }");
        auto *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(6, 4, 6, 4);

        auto *numLabel = new QLabel(QString::number(i + 1));
        numLabel->setStyleSheet("color:#888; font-size:8pt; min-width:20px;");
        numLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        auto *wordLabel = new QLabel(wordList.at(i));
        wordLabel->setStyleSheet(
            "font-size:11pt; font-weight:bold; font-family:'Courier New',monospace; color:#1a1a1a;");
        wordLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

        cellLayout->addWidget(numLabel);
        cellLayout->addWidget(wordLabel, 1);
        grid->addWidget(cell, row, col);
    }

    gridWidget->show();
    if (placeholder) placeholder->hide();

    // Enable action buttons
    auto *copyBtn   = findChild<QPushButton*>("copyBtn");
    auto *verifyBtn = findChild<QPushButton*>("verifyBtn");
    auto *revealBtn = findChild<QPushButton*>("revealBtn");
    if (copyBtn)   copyBtn->setEnabled(true);
    if (verifyBtn) verifyBtn->setEnabled(true);
    if (revealBtn) revealBtn->setEnabled(false); // already revealed
}

void SeedPhraseDialog::onCopyClicked()
{
    if (m_currentMnemonic.isEmpty()) return;

    QClipboard *cb = QApplication::clipboard();
    cb->setText(m_currentMnemonic);

    auto *copyBtn = findChild<QPushButton*>("copyBtn");
    if (copyBtn) {
        copyBtn->setText(tr("Copied! (clears in 30s)"));
        copyBtn->setEnabled(false);
    }

    // Auto-clear clipboard after 30 seconds
    m_clipboardTimer.start(30000);
}

void SeedPhraseDialog::onClipboardClearTick()
{
    QClipboard *cb = QApplication::clipboard();
    // Only clear if our mnemonic is still on the clipboard
    if (cb->text() == m_currentMnemonic)
        cb->clear();

    auto *copyBtn = findChild<QPushButton*>("copyBtn");
    if (copyBtn) {
        copyBtn->setText(tr("Copy to Clipboard"));
        copyBtn->setEnabled(!m_currentMnemonic.isEmpty());
    }
}

void SeedPhraseDialog::onVerifyClicked()
{
    // Ask user to re-enter the mnemonic
    bool ok = false;
    QString entered = QInputDialog::getMultiLineText(
        this,
        tr("Verify Seed Phrase"),
        tr("Enter your seed phrase (space-separated words) to confirm you "
           "recorded it correctly:"),
        QString(), &ok);

    if (!ok || entered.trimmed().isEmpty()) return;

    entered = entered.simplified(); // normalise whitespace

    if (entered == m_currentMnemonic) {
        QMessageBox::information(this, tr("Verification Successful"),
            tr("✓ Your seed phrase matches. It is recorded correctly."));
    } else {
        SecureString ss(entered.toStdString().begin(), entered.toStdString().end());
        if (!BIP39Wallet::validateMnemonic(ss)) {
            QMessageBox::critical(this, tr("Invalid Mnemonic"),
                tr("The phrase you entered is not a valid BIP39 mnemonic "
                   "(checksum failed or unknown words)."));
        } else {
            QMessageBox::warning(this, tr("Mismatch"),
                tr("The phrase you entered does not match your wallet's recovery phrase. "
                   "Please check your written copy."));
        }
    }
}

// ── clearMnemonic ─────────────────────────────────────────────────────────────

void SeedPhraseDialog::clearMnemonic()
{
    if (!m_currentMnemonic.isEmpty()) {
        // Overwrite Qt's copy-on-write string in place
        for (int i = 0; i < m_currentMnemonic.size(); ++i)
            m_currentMnemonic[i] = QChar('\0');
        m_currentMnemonic.clear();
    }

    m_countdownTimer.stop();
    m_clipboardTimer.stop();

    // Reset grid
    auto *gridWidget  = findChild<QWidget*>("wordGrid");
    auto *placeholder = findChild<QLabel*>("placeholderLabel");
    auto *copyBtn     = findChild<QPushButton*>("copyBtn");
    auto *verifyBtn   = findChild<QPushButton*>("verifyBtn");
    auto *revealBtn   = findChild<QPushButton*>("revealBtn");

    if (gridWidget)  gridWidget->hide();
    if (placeholder) placeholder->show();
    if (copyBtn)   { copyBtn->setText(tr("Copy to Clipboard")); copyBtn->setEnabled(false); }
    if (verifyBtn)   verifyBtn->setEnabled(false);
    if (revealBtn)   revealBtn->setEnabled(true);
}

// ── Window events ─────────────────────────────────────────────────────────────

void SeedPhraseDialog::closeEvent(QCloseEvent *event)
{
    clearMnemonic();
    QDialog::closeEvent(event);
}

void SeedPhraseDialog::hideEvent(QHideEvent *event)
{
    clearMnemonic();
    QDialog::hideEvent(event);
}

void SeedPhraseDialog::onRotateClicked()
{
    if (!m_model) return;

    // Hide the current phrase before launching rotation -- once rotation
    // succeeds the displayed phrase will no longer be valid.
    clearMnemonic();

    RotatePhraseDialog dlg(m_model, this);
    dlg.exec();

    // After the rotate dialog closes (whether it succeeded, failed, or
    // was cancelled), we leave the seed phrase dialog in its idle state
    // -- the user can click "Reveal" again to view the (now possibly new)
    // phrase derived from the current vMasterKey.
}