// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// recoveryphraseupgradedialog.cpp
// See header for design notes.

#include "recoveryphraseupgradedialog.h"
#include "walletmodel.h"
#include "seedphrasedialog.h"
#include "allocators/securestring.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QString>
#include <QFont>

#include <openssl/crypto.h>

// ---------------------------------------------------------------------------
// Strings -- consolidated here so they're easy to review/translate.
// ---------------------------------------------------------------------------

static const QString kTitle =
    QObject::tr("Set up recovery phrase");

static const QString kBodyHtml = QObject::tr(
    "<p><b>Your wallet does not yet have a 24-word recovery phrase.</b></p>"

    "<p>A recovery phrase is a list of 24 words derived from your wallet's "
    "encryption key.  If you ever forget your password, the phrase lets "
    "you regain access to this wallet file.</p>"

    "<p><b>Important:</b> the recovery phrase works alongside your "
    "<code>wallet.dat</code> file &mdash; not as a replacement for it. "
    "You must continue to back up <code>wallet.dat</code> separately. "
    "Losing the file means losing your funds, even if you have the phrase.</p>"

    "<p>Setting up the phrase takes a few seconds and requires no password. "
    "You'll see the 24 words once and should write them down on paper "
    "immediately.</p>"

    "<p>What would you like to do?</p>");

static const QString kDeclineConfirmHtml = QObject::tr(
    "<p>Are you sure?  Without a recovery phrase, the only way to unlock "
    "this wallet will be your password.  If you forget the password, "
    "your funds will be unrecoverable.</p>"

    "<p>You can set up the recovery phrase any time from "
    "<b>Settings &rarr; Recovery Phrase</b>.</p>");

static const QString kSetupSuccessFollowupHtml = QObject::tr(
    "<p>Recovery phrase generated.</p>"

    "<p>The next dialog will display your 24 words.  Write them down "
    "before closing it &mdash; this is your only opportunity to see "
    "them in this convenient format.  (You can re-display them later "
    "from <b>Settings &rarr; Recovery Phrase</b>, but you'll need your "
    "password.)</p>");

static const QString kSetupFailureHtml = QObject::tr(
    "<p>Failed to set up the recovery phrase.</p>"

    "<p>This usually means the wallet is in an unexpected state.  "
    "You can try again from <b>Settings &rarr; Recovery Phrase</b>, "
    "or restart the wallet and try again.</p>");

// ---------------------------------------------------------------------------

RecoveryPhraseUpgradeDialog::RecoveryPhraseUpgradeDialog(WalletModel *model,
                                                         QWidget *parent)
    : QDialog(parent),
      m_model(model)
{
    setWindowTitle(kTitle);
    setModal(true);
    setMinimumWidth(560);
    setMaximumWidth(640);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);

    // Headline
    QLabel *headline = new QLabel(kTitle, this);
    QFont hFont = headline->font();
    hFont.setPointSize(hFont.pointSize() + 3);
    hFont.setBold(true);
    headline->setFont(hFont);
    layout->addWidget(headline);

    // Body
    QLabel *body = new QLabel(kBodyHtml, this);
    body->setTextFormat(Qt::RichText);
    body->setWordWrap(true);
    layout->addWidget(body);

    layout->addSpacing(8);

    // Buttons
    QPushButton *setupBtn = new QPushButton(tr("Set up now"), this);
    setupBtn->setDefault(true);
    setupBtn->setAutoDefault(true);

    QPushButton *laterBtn = new QPushButton(tr("Maybe later"), this);

    QPushButton *neverBtn = new QPushButton(tr("Dismiss for this wallet"), this);
    neverBtn->setStyleSheet("QPushButton { color:#888; }");

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addWidget(neverBtn);    // bottom-left, low-emphasis
    btnRow->addStretch();
    btnRow->addWidget(laterBtn);
    btnRow->addWidget(setupBtn);    // bottom-right, primary
    layout->addLayout(btnRow);

    connect(setupBtn, &QPushButton::clicked,
            this,     &RecoveryPhraseUpgradeDialog::onSetUpNow);
    connect(laterBtn, &QPushButton::clicked,
            this,     &RecoveryPhraseUpgradeDialog::onMaybeLater);
    connect(neverBtn, &QPushButton::clicked,
            this,     &RecoveryPhraseUpgradeDialog::onDontAskAgain);
}

void RecoveryPhraseUpgradeDialog::onMaybeLater()
{
    // Nothing to record -- next unlock will see the same wallet state
    // and re-trigger the prompt.
    reject();
}

void RecoveryPhraseUpgradeDialog::onDontAskAgain()
{
    // Confirm the decline so the user understands the consequence.
    QMessageBox confirm(this);
    confirm.setWindowTitle(tr("Skip recovery phrase setup"));
    confirm.setIcon(QMessageBox::Warning);
    confirm.setTextFormat(Qt::RichText);
    confirm.setText(kDeclineConfirmHtml);

    QPushButton *yesBtn = confirm.addButton(
        tr("Yes, dismiss for this wallet"), QMessageBox::AcceptRole);
    QPushButton *cancelBtn = confirm.addButton(
        tr("Cancel"), QMessageBox::RejectRole);
    confirm.setDefaultButton(cancelBtn);
    confirm.exec();

    if (confirm.clickedButton() == yesBtn) {
        if (m_model) {
            m_model->setRecoveryPhraseUpgradeDeclined();
        }
        accept();
    }
    // else: stay in the upgrade dialog; user can pick a different option
}

void RecoveryPhraseUpgradeDialog::onSetUpNow()
{
    if (!m_model) {
        reject();
        return;
    }

    // Step 1: Add CMasterKey[2].  The wallet is unlocked at this point
    // (the upgrade prompt fires in response to a successful unlock),
    // so vMasterKey is in memory and addMnemonicMasterKey() can derive
    // the phrase from it directly with no password prompt.
    if (!m_model->addMnemonicMasterKey()) {
        QMessageBox::critical(this, kTitle, kSetupFailureHtml);
        // Don't accept(), don't decline -- leave the dialog open so the
        // user can try again or pick another option.
        return;
    }

    // Step 2: Re-derive the phrase for display.
    SecureString mnemonic;
    if (!m_model->getCurrentMnemonic(mnemonic)) {
        // Phrase entry was added but we couldn't read it back.  Highly
        // unlikely; if it happens, tell the user to use the menu later.
        QMessageBox::warning(this, kTitle,
            tr("Recovery phrase was generated but could not be displayed.  "
               "You can view it from <b>Settings &rarr; Recovery Phrase</b>."));
        accept();
        return;
    }

    QString mnWords = QString::fromUtf8(mnemonic.data(),
                                         static_cast<int>(mnemonic.size()));
    OPENSSL_cleanse(const_cast<char*>(mnemonic.data()), mnemonic.size());

    // Step 3: Brief informational message before the phrase appears,
    // so the user knows the next dialog needs their attention.
    QMessageBox info(this);
    info.setWindowTitle(kTitle);
    info.setIcon(QMessageBox::Information);
    info.setTextFormat(Qt::RichText);
    info.setText(kSetupSuccessFollowupHtml);
    info.addButton(tr("Show recovery phrase"), QMessageBox::AcceptRole);
    info.exec();

    // Step 4: Display in the same dialog used for fresh-encrypt phrases.
    SeedPhraseDialog phraseDlg(m_model, parentWidget(),
                                SeedPhraseDialog::Mode::FirstTimeAutoReveal,
                                mnWords);
    phraseDlg.exec();

    // Wipe the local QString -- the dialog has already wiped its own copies.
    {
        QChar* d = const_cast<QChar*>(mnWords.constData());
        for (int i = 0; i < mnWords.size(); ++i) d[i] = QChar('\0');
        mnWords.clear();
    }

    accept();
}
