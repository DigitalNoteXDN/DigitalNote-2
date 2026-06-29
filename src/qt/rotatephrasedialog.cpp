// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// rotatephrasedialog.cpp
// See rotatephrasedialog.h for the design notes and user flow.

#include "rotatephrasedialog.h"
#include "walletmodel.h"
#include "seedphrasedialog.h"
#include "guiutil.h"
#include "allocators/securestring.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QTimer>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QFont>

#include <openssl/crypto.h>

// ---------------------------------------------------------------------------
// Hardcoded UI strings -- written here rather than in a .ui file so they're
// easy to review as one block.  These are deliberately wordy: this is a
// destructive security operation and the user needs to understand it.
// ---------------------------------------------------------------------------

static const QString kConsentTitle =
    QObject::tr("Replace recovery phrase — compromised phrase remediation");

static const QString kConsentText = QObject::tr(
    "<p><b>This rotates your wallet's encryption key.</b> A new 24-word "
    "recovery phrase will be generated. The old phrase will no longer "
    "decrypt this wallet file.</p>"

    "<p><b>When this helps:</b> Use this if you believe your old phrase "
    "has been seen by someone else (someone glanced at the paper, you "
    "typed it into a suspicious website, etc.) <i>and</i> you still have "
    "exclusive control of your wallet file.</p>"

    "<p><b>When this does NOT help:</b></p>"
    "<ul>"
    "<li>If anyone has obtained a copy of your <code>wallet.dat</code> "
    "file, they can still decrypt it with the old phrase. Rotation only "
    "protects future access, not copies that already exist elsewhere. "
    "If you suspect your wallet file has been copied, you should "
    "<b>move your funds to a new wallet</b> instead.</li>"
    "<li>If your computer has been compromised by malware, the new phrase "
    "may also be observed when it is generated. Clean the machine first.</li>"
    "</ul>"

    "<p><b>Before continuing:</b></p>"
    "<ol>"
    "<li>Make sure you have a current backup of <code>wallet.dat</code>.</li>"
    "<li>Have pen and paper ready &mdash; you must write down the new "
    "phrase as soon as it is shown.</li>"
    "<li>The old phrase will stop working <b>immediately</b> after this "
    "operation completes.</li>"
    "</ol>"

    "<p>Do you want to proceed?</p>");

static const QString kPasswordPromptTitle =
    QObject::tr("Confirm wallet password");

static const QString kPasswordPromptText = QObject::tr(
    "Enter your wallet password to authorise rotation.\n\n"
    "Your password is not changing — we just need it to re-encrypt "
    "the new master key.");

static const QString kFailureBadPassword = QObject::tr(
    "The password you entered is incorrect. The wallet has not been "
    "modified. You can try again from the seed phrase dialog.");

static const QString kFailureGeneric = QObject::tr(
    "Recovery phrase rotation failed. The wallet file has not been "
    "modified. This is unexpected — please check the debug log and "
    "try again.");

static const QString kDoneIntro = QObject::tr(
    "<p><b>Your new recovery phrase is shown below.</b></p>"

    "<p>Write all 24 words down on paper now, in order. Store the paper "
    "somewhere safe and offline.</p>"

    "<p>The old recovery phrase no longer works for this wallet. You can "
    "still unlock with your password, and you can use the new phrase to "
    "recover access if you ever forget your password.</p>");

static const int kHoldSeconds = 10;

// ---------------------------------------------------------------------------

RotatePhraseDialog::RotatePhraseDialog(WalletModel *model, QWidget *parent)
    : QDialog(parent),
      m_model(model),
      m_stage(StageInit),
      m_phraseView(nullptr),
      m_countdownLabel(nullptr),
      m_ackCheck(nullptr),
      m_closeBtn(nullptr),
      m_countdownTimer(nullptr),
      m_secondsRemaining(kHoldSeconds)
{
    setWindowTitle(tr("Rotate recovery phrase"));
    setModal(true);
    setMinimumWidth(560);

    // Attempt the flow.  The constructor returns either with the dialog
    // ready to display the new phrase (StageDone) or the failure message
    // (StageFailed).  If the user cancels at any consent step, we just
    // close immediately without showing anything.
    runFlow();
}

RotatePhraseDialog::~RotatePhraseDialog()
{
    clearAllSensitive();
}

void RotatePhraseDialog::runFlow()
{
    // 1. Consent
    if (!showConsentDialog()) {
        // User cancelled at the wall-of-text stage.  Do nothing further;
        // close the dialog as soon as exec() runs.
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }

    // 2. Password prompt
    QString password;
    if (!promptForPassword(password)) {
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }

    // 3. Ensure wallet is unlocked.  RotateMnemonicMasterKey() requires
    //    vMasterKey to already be in memory (it verifies the password
    //    against an existing CMasterKey envelope but does not itself
    //    unlock the keystore).  If the user opened the seed phrase
    //    dialog without unlocking the wallet first, we have to unlock
    //    here using the password they just entered.
    //
    // NOTE: Use the .assign(...c_str()) idiom (matching askpassphrasedialog)
    // to convert QString -> SecureString.  The previous code used
    //   SecureString s(qs.toStdString().begin(), qs.toStdString().end());
    // which is undefined behaviour: each toStdString() returns a *different*
    // temporary, so begin() and end() point into different memory ranges
    // that are both destroyed at the end of the expression.
    SecureString securePassword;
    securePassword.reserve(1024);
    securePassword.assign(password.toStdString().c_str());

    // Best-effort wipe of QString contents.  QString isn't truly secure,
    // but we shouldn't leave the password in a Qt heap allocation either.
    {
        QChar* d = const_cast<QChar*>(password.constData());
        for (int i = 0; i < password.size(); ++i) d[i] = QChar('\0');
        password.clear();
    }

    bool wasLocked = (m_model->getEncryptionStatus() == WalletModel::Locked);
    if (wasLocked) {
        if (!m_model->setWalletLocked(false, securePassword)) {
            // Wrong password (or some other unlock failure).  Treat as
            // an authentication failure rather than a rotation failure
            // -- clearer message for the user.
            OPENSSL_cleanse(const_cast<char*>(securePassword.data()),
                            securePassword.size());
            buildFailureUi(kFailureBadPassword);
            return;
        }
    }

    // 4. Rotate
    SecureString newPhrase;
    bool ok = m_model->rotateRecoveryPhrase(securePassword, newPhrase);
    OPENSSL_cleanse(const_cast<char*>(securePassword.data()),
                    securePassword.size());

    // Restore prior lock state.  On success we want the wallet to be in
    // the same lock state as when the user opened this dialog (so they're
    // not surprised by an unlocked wallet afterward).  On failure we
    // definitely want to re-lock, because we unlocked specifically for
    // this operation and shouldn't leave the wallet exposed.
    if (wasLocked) {
        m_model->setWalletLocked(true);
    }

    if (!ok) {
        buildFailureUi(kFailureGeneric);
        return;
    }

    // 4. Display new phrase using the same dialog we use for fresh-encrypt
    //    and recovery-upgrade flows.  Visual consistency: every "here's your
    //    new phrase" moment in the wallet looks identical.
    QString phraseStr = QString::fromUtf8(newPhrase.data(),
                                          static_cast<int>(newPhrase.size()));
    OPENSSL_cleanse(const_cast<char*>(newPhrase.data()), newPhrase.size());

    // Hide ourselves before opening the SeedPhraseDialog so the user only
    // sees one window.  We accept() at the end of this function so this
    // dialog disposes cleanly.
    hide();

    SeedPhraseDialog phraseDlg(m_model, parentWidget(),
                                SeedPhraseDialog::Mode::FirstTimeAutoReveal,
                                phraseStr);
    phraseDlg.exec();

    // Wipe the local QString -- the SeedPhraseDialog has already wiped its
    // own internal copies on close.
    {
        QChar* d = const_cast<QChar*>(phraseStr.constData());
        for (int i = 0; i < phraseStr.size(); ++i) d[i] = QChar('\0');
        phraseStr.clear();
    }

    accept();
    return;
}

bool RotatePhraseDialog::showConsentDialog()
{
    QMessageBox box(parentWidget());
    box.setWindowTitle(kConsentTitle);
    box.setIcon(QMessageBox::Warning);
    box.setTextFormat(Qt::RichText);
    box.setText(kConsentText);
    QPushButton *proceed = box.addButton(tr("Continue with rotation"),
                                          QMessageBox::AcceptRole);
    QPushButton *cancel  = box.addButton(tr("Cancel"),
                                          QMessageBox::RejectRole);
    Q_UNUSED(cancel);
    box.setDefaultButton(cancel);  // safer default
    box.exec();
    return box.clickedButton() == proceed;
}

bool RotatePhraseDialog::promptForPassword(QString &outPassword)
{
    // Use a custom QDialog rather than QInputDialog::getText -- the latter
    // doesn't word-wrap its prompt text, so a multi-line message renders
    // as one absurdly-wide line stretched across the screen.

    QDialog dlg(parentWidget());
    dlg.setWindowTitle(kPasswordPromptTitle);
    dlg.setModal(true);
    dlg.setMinimumWidth(440);
    dlg.setMaximumWidth(520);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 16);

    QLabel *prompt = new QLabel(kPasswordPromptText, &dlg);
    prompt->setWordWrap(true);
    layout->addWidget(prompt);

    QLineEdit *input = new QLineEdit(&dlg);
    input->setEchoMode(QLineEdit::Password);
    layout->addWidget(input);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    QPushButton *cancelBtn = new QPushButton(tr("Cancel"), &dlg);
    QPushButton *okBtn     = new QPushButton(tr("OK"), &dlg);
    okBtn->setDefault(true);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    layout->addLayout(btnRow);

    QObject::connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(input,     &QLineEdit::returnPressed, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted) {
        outPassword.clear();
        return false;
    }

    outPassword = input->text();
    // Best-effort wipe of the QLineEdit contents.
    input->clear();
    return !outPassword.isEmpty();
}

void RotatePhraseDialog::buildDoneUi(const QString &newPhrase)
{
    m_stage = StageDone;

    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *intro = new QLabel(kDoneIntro, this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    m_phraseView = new QTextEdit(this);
    m_phraseView->setReadOnly(true);
    m_phraseView->setPlainText(newPhrase);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(mono.pointSize() + 1);
    m_phraseView->setFont(mono);
    m_phraseView->setMinimumHeight(120);
    layout->addWidget(m_phraseView);

    m_countdownLabel = new QLabel(this);
    m_countdownLabel->setAlignment(Qt::AlignCenter);
    m_countdownLabel->setText(tr("Please wait %1 seconds before closing...")
                                  .arg(kHoldSeconds));
    layout->addWidget(m_countdownLabel);

    m_ackCheck = new QCheckBox(
        tr("I have written down all 24 words on paper"), this);
    m_ackCheck->setEnabled(false);
    connect(m_ackCheck, &QCheckBox::toggled,
            this, &RotatePhraseDialog::onAcknowledgeToggled);
    layout->addWidget(m_ackCheck);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_closeBtn = new QPushButton(tr("Close"), this);
    m_closeBtn->setEnabled(false);
    connect(m_closeBtn, &QPushButton::clicked,
            this, &RotatePhraseDialog::onCloseClicked);
    btnRow->addWidget(m_closeBtn);
    layout->addLayout(btnRow);

    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout,
            this, &RotatePhraseDialog::onCountdownTick);
    m_secondsRemaining = kHoldSeconds;
    m_countdownTimer->start();
}

void RotatePhraseDialog::buildFailureUi(const QString &failureMessage)
{
    m_stage = StageFailed;

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *msg = new QLabel(failureMessage, this);
    msg->setWordWrap(true);
    msg->setMinimumWidth(400);
    layout->addWidget(msg);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    QPushButton *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}

void RotatePhraseDialog::onCountdownTick()
{
    if (--m_secondsRemaining <= 0) {
        m_countdownTimer->stop();
        m_countdownLabel->setText(
            tr("You may close this dialog after confirming below."));
        m_ackCheck->setEnabled(true);
    } else {
        m_countdownLabel->setText(tr("Please wait %1 seconds before closing...")
                                      .arg(m_secondsRemaining));
    }
}

void RotatePhraseDialog::onAcknowledgeToggled(bool checked)
{
    if (m_closeBtn) m_closeBtn->setEnabled(checked);
}

void RotatePhraseDialog::onCloseClicked()
{
    accept();
}

void RotatePhraseDialog::clearAllSensitive()
{
    if (m_phraseView) {
        // Overwrite then clear -- QTextEdit's document allocates on the heap
        // and we want the bytes wiped before the dialog destructor frees it.
        QString s = m_phraseView->toPlainText();
        QChar* d = const_cast<QChar*>(s.constData());
        for (int i = 0; i < s.size(); ++i) d[i] = QChar('*');
        m_phraseView->setPlainText(s);
        m_phraseView->clear();
    }
    // Best-effort: drop any clipboard contents we own.
    QClipboard *cb = QApplication::clipboard();
    if (cb) {
        // Only clear if we suspect we put text there; we don't currently
        // offer copy-to-clipboard in this dialog, but this is harmless.
    }
}

void RotatePhraseDialog::closeEvent(QCloseEvent *event)
{
    clearAllSensitive();
    QDialog::closeEvent(event);
}

void RotatePhraseDialog::hideEvent(QHideEvent *event)
{
    clearAllSensitive();
    QDialog::hideEvent(event);
}
