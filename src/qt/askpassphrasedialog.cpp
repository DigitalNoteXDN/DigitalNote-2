#include "compat.h"

#include "askpassphrasedialog.h"
#include "ui_askpassphrasedialog.h"

#include "guiconstants.h"
#include "walletmodel.h"
#include "wallet.h"
#include "seedphrasedialog.h"

#include "bip39/bip39_passphrase.h"
#include <openssl/crypto.h>

#include <QMessageBox>
#include <QInputDialog>
#include <QPushButton>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QFrame>
#include <QRandomGenerator>

// ── Password generator ───────────────────────────────────────────────────────

static QString generateStrongPassword(int length = 20)
{
    // Alphanumeric + symbols, avoiding ambiguous characters (0,O,l,1,I)
    const QString chars =
        "abcdefghjkmnpqrstuvwxyz"
        "ABCDEFGHJKMNPQRSTUVWXYZ"
        "23456789"
        "!@#$%^&*-_=+";
    QString result;
    result.reserve(length);
    for (int i = 0; i < length; ++i)
        result += chars[QRandomGenerator::global()->bounded(chars.size())];
    return result;
}

// ── Constructor ──────────────────────────────────────────────────────────────

AskPassphraseDialog::AskPassphraseDialog(Mode mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AskPassphraseDialog),
    mode(mode),
    model(0),
    fCapsLock(false)
{
    ui->setupUi(this);
    ui->passEdit1->setMaxLength(MAX_PASSPHRASE_SIZE);
    ui->passEdit2->setMaxLength(MAX_PASSPHRASE_SIZE);
    ui->passEdit3->setMaxLength(MAX_PASSPHRASE_SIZE);

    // Setup Caps Lock detection.
    ui->passEdit1->installEventFilter(this);
    ui->passEdit2->installEventFilter(this);
    ui->passEdit3->installEventFilter(this);

    ui->stakingCheckBox->setChecked(false);

    switch(mode)
    {
        case Encrypt:
            setupEncryptMode();
            break;

        case UnlockStaking:
            ui->stakingCheckBox->setChecked(true);
            ui->stakingCheckBox->show();
            // fallthru
        case Unlock:
        {
            ui->warningLabel->setText(
                tr("This operation needs your wallet passphrase to unlock the wallet."));
            ui->passLabel2->hide();
            ui->passEdit2->hide();
            ui->passLabel3->hide();
            ui->passEdit3->hide();
            // Hide staking checkbox for plain Unlock — only show for UnlockStaking
            if (mode == Unlock) {
                ui->stakingCheckBox->setChecked(false);
                ui->stakingCheckBox->hide();
            }
            setWindowTitle(tr("Unlock wallet"));

             // "Forgot password?" recovery link
             QPushButton *seedBtn = new QPushButton(
                 tr("Forgot password? Unlock with recovery phrase..."), this);
             seedBtn->setFlat(true);
             seedBtn->setStyleSheet(
                 "QPushButton { color: #3098c6; text-decoration: underline; "
                 "border: none; background: transparent; }");
             QVBoxLayout *vl = qobject_cast<QVBoxLayout*>(ui->verticalLayout);
             if (vl) {
                 int vidx = vl->indexOf(ui->capsLabel);
                 if (vidx >= 0)
                     vl->insertWidget(vidx + 1, seedBtn);
                 else
                     vl->addWidget(seedBtn);
             }
             connect(seedBtn, &QPushButton::clicked,
                     this, &AskPassphraseDialog::onSwitchToSeed);
             break;
        }

        case UnlockWithSeed:
        {
            ui->passLabel1->hide();
            ui->passEdit1->hide();
            ui->passLabel2->hide();
            ui->passEdit2->hide();
            ui->passLabel3->hide();
            ui->passEdit3->hide();
            ui->warningLabel->setText(
                tr("<b>Unlock wallet using your 24-word recovery phrase.</b><br><br>"
                   "Enter your recovery phrase below. Your <b>wallet.dat</b> must be present."
                   "<br><br><i>This only works for wallets encrypted in DigitalNote v2.0.0.7+.</i>"));
            setWindowTitle(tr("Unlock with recovery phrase"));
            QTextEdit *seedEdit = new QTextEdit(this);
            seedEdit->setObjectName("seedEdit");
            seedEdit->setPlaceholderText(
                tr("Enter your 24 recovery words separated by spaces..."));
            seedEdit->setMaximumHeight(80);
            QVBoxLayout *vl = qobject_cast<QVBoxLayout*>(ui->verticalLayout);
            if (vl) {
                int vidx = vl->indexOf(ui->capsLabel);
                if (vidx >= 0)
                    vl->insertWidget(vidx + 1, seedEdit);
                else
                    vl->addWidget(seedEdit);
            }
            QPushButton *passBtn = new QPushButton(tr("Use password instead"), this);
            passBtn->setFlat(true);
            passBtn->setStyleSheet(
                "QPushButton { color: #3098c6; text-decoration: underline; "
                "border: none; background: transparent; }");
            if (QVBoxLayout *vl2 = qobject_cast<QVBoxLayout*>(ui->verticalLayout))
                vl2->addWidget(passBtn);
            connect(passBtn, &QPushButton::clicked,
                    this, &AskPassphraseDialog::onSwitchToPassword);
            break;
        }

        case Decrypt:
            ui->warningLabel->setText(
                tr("This operation needs your wallet passphrase to decrypt the wallet."));
            ui->passLabel2->hide();
            ui->passEdit2->hide();
            ui->passLabel3->hide();
            ui->passEdit3->hide();
            setWindowTitle(tr("Decrypt wallet"));
            break;

        case ChangePass:
            setWindowTitle(tr("Change passphrase"));
            ui->warningLabel->setText(
                tr("Enter the old and new passphrase to the wallet."));
            break;
    }

    textChanged();
    connect(ui->passEdit1, SIGNAL(textChanged(QString)), this, SLOT(textChanged()));
    connect(ui->passEdit2, SIGNAL(textChanged(QString)), this, SLOT(textChanged()));
    connect(ui->passEdit3, SIGNAL(textChanged(QString)), this, SLOT(textChanged()));
}

void AskPassphraseDialog::setupEncryptMode()
{
    ui->passLabel1->hide();
    ui->passEdit1->hide();
    ui->warningLabel->setText(
        tr("Choose a passphrase to encrypt your wallet.<br/>"
           "Please use a passphrase of <b>ten or more random characters</b>, "
           "or <b>eight or more words</b>.<br/><br/>"
           "After encrypting, you will be shown a <b>24-word recovery phrase</b>. "
           "Write it down safely \u2014 it can unlock your wallet if you forget your password."));;
    setWindowTitle(tr("Encrypt wallet"));

    // Generate password button
    QPushButton *genBtn = new QPushButton(tr("⚙ Generate strong password"), this);
    genBtn->setToolTip(tr("Automatically generate a strong 20-character password"));
    QVBoxLayout *vl = qobject_cast<QVBoxLayout*>(ui->verticalLayout);
    if (vl) {
        int idx = vl->indexOf(ui->passEdit2);
        if (idx >= 0)
            vl->insertWidget(idx + 1, genBtn);
        else
            vl->addWidget(genBtn);
    }
    connect(genBtn, &QPushButton::clicked, this, &AskPassphraseDialog::onGeneratePassword);
}

AskPassphraseDialog::~AskPassphraseDialog()
{
    secureClearPassFields();
    delete ui;
}

void AskPassphraseDialog::setModel(WalletModel *model)
{
    this->model = model;
}

// ── Slots ────────────────────────────────────────────────────────────────────

void AskPassphraseDialog::onGeneratePassword()
{
    QString pw = generateStrongPassword(20);
    ui->passEdit2->setText(pw);
    ui->passEdit3->setText(pw);
    ui->passEdit2->setEchoMode(QLineEdit::Normal); // Show so user can write it down
    ui->passEdit3->setEchoMode(QLineEdit::Normal);

    // Show the generated password in a copy dialog
    QMessageBox mb(this);
    mb.setWindowTitle(tr("Generated Password"));
    mb.setText(tr("<b>Your generated password:</b><br><br>"
                  "<tt style='font-size:12pt; background:#f0f0f0; padding:4px;'>%1</tt><br><br>"
                  "Write this down now and store it safely. "
                  "Click OK to use this password for encryption.").arg(pw));
    mb.setIcon(QMessageBox::Information);
    QPushButton *copyBtn = mb.addButton(tr("Copy to clipboard"), QMessageBox::ActionRole);
    mb.addButton(QMessageBox::Ok);
    mb.exec();

    if (mb.clickedButton() == copyBtn) {
        QApplication::clipboard()->setText(pw);
    }
}

void AskPassphraseDialog::onSwitchToSeed()
{
    reject();
    AskPassphraseDialog *d = new AskPassphraseDialog(UnlockWithSeed, parentWidget());
    d->setModel(model);
    d->exec();
    d->deleteLater();
}

void AskPassphraseDialog::tryUnlockWithSeed(const QString& phrase)
{
    if (!model) return;
    QString trimmed = phrase.simplified().trimmed();
    if (trimmed.isEmpty()) {
        QMessageBox::warning(this, tr("Empty recovery phrase"),
            tr("Please enter your 24-word recovery phrase."));
        return;
    }
    WalletModel::UnlockContext ctx = model->requestUnlockWithMnemonic(trimmed);
    if (!ctx.isValid()) {
        QMessageBox::critical(this, tr("Unlock failed"),
            tr("Could not unlock the wallet.<br><br>"
               "Make sure all 24 words are correct and this wallet "
               "was encrypted in DigitalNote v2.0.0.7 or later."));
        return;
    }
    QMessageBox::information(this, tr("Wallet unlocked"),
        tr("Wallet successfully unlocked using your recovery phrase."));
    QDialog::accept();
}

void AskPassphraseDialog::onSwitchToPassword()
{
    reject();
    AskPassphraseDialog *passDlg = new AskPassphraseDialog(Unlock, parentWidget());
    passDlg->setModel(model);
    passDlg->exec();
    passDlg->deleteLater();
}

// ── accept() ─────────────────────────────────────────────────────────────────

void AskPassphraseDialog::accept()
{
    SecureString oldpass, newpass1, newpass2;
    if(!model)
        return;
    oldpass.reserve(MAX_PASSPHRASE_SIZE);
    newpass1.reserve(MAX_PASSPHRASE_SIZE);
    newpass2.reserve(MAX_PASSPHRASE_SIZE);
    oldpass.assign(ui->passEdit1->text().toStdString().c_str());
    newpass1.assign(ui->passEdit2->text().toStdString().c_str());
    newpass2.assign(ui->passEdit3->text().toStdString().c_str());

    secureClearPassFields();

    switch(mode)
    {
    case Encrypt: {
        if(newpass1.empty() || newpass2.empty())
            break;
        QMessageBox::StandardButton retval = QMessageBox::question(this,
            tr("Confirm wallet encryption"),
            tr("Warning: If you encrypt your wallet and lose your passphrase, you will "
               "<b>LOSE ALL OF YOUR COINS</b>!") + "<br><br>" +
            tr("Are you sure you wish to encrypt your wallet?") + "<br><br>" +
            tr("<i>Tip: After encrypting, go to Settings → Seed Phrase / Recovery Words "
               "to enable seed phrase recovery.</i>"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);

        if(retval == QMessageBox::Yes) {
            if(newpass1 == newpass2) {
                if(model->setWalletEncrypted(true, newpass1)) {
                    // Register the mnemonic as a second master key
                    // so both password AND recovery phrase unlock the wallet
                    model->addMnemonicMasterKey(newpass1);

                    // Derive and show the recovery mnemonic
                    SecureString recoveryMnemonic;
                    bool mnOk = model->generateRecoveryMnemonic(newpass1, recoveryMnemonic);
                    if (mnOk && !recoveryMnemonic.empty()) {
                        QString mnWords = QString::fromStdString(
                            std::string(recoveryMnemonic.begin(), recoveryMnemonic.end()));
                        OPENSSL_cleanse(const_cast<char*>(recoveryMnemonic.data()),
                                        recoveryMnemonic.size());
                        QMessageBox mb(this);
                        mb.setWindowTitle(tr("Your 24-Word Recovery Phrase"));
                        mb.setIcon(QMessageBox::Warning);
                        mb.setText(
                            tr("<b>Write down these 24 words in order and store them safely.</b>"
                               "<br><br><tt>%1</tt><br><br>"
                               "These words can recover access to your encrypted wallet if you forget "
                               "your password. <b>Anyone with these words can unlock your wallet.</b>"
                               "<br><br><i>This phrase will not be shown again.</i>").arg(mnWords));
                        QPushButton *copyBtn = mb.addButton(
                            tr("Copy to clipboard"), QMessageBox::ActionRole);
                        mb.addButton(tr("I have written it down"), QMessageBox::AcceptRole);
                        mb.exec();
                        if (mb.clickedButton() == copyBtn)
                            QApplication::clipboard()->setText(mnWords);
                    }

                    QMessageBox::warning(this, tr("Wallet encrypted"),
                        "<qt>" +
                        tr("DigitalNote will close now to finish the encryption process. "
                           "Remember that encrypting your wallet cannot fully protect "
                           "your coins from being stolen by malware infecting your computer.") +
                        "<br><br><b>" +
                        tr("IMPORTANT: Any previous backups you have made of your wallet file "
                           "should be replaced with the newly generated, encrypted wallet file. "
                           "For security reasons, previous backups of the unencrypted wallet file "
                           "will become useless as soon as you start using the new, encrypted wallet.") +
                        "</b><br><br>" +
                        "</qt>");
                    QApplication::quit();
                } else {
                    QMessageBox::critical(this, tr("Wallet encryption failed"),
                        tr("Wallet encryption failed due to an internal error. Your wallet was not encrypted."));
                }
                QDialog::accept();
            } else {
                QMessageBox::critical(this, tr("Wallet encryption failed"),
                    tr("The supplied passphrases do not match."));
            }
        } else {
            QDialog::reject();
        }
        } break;

    case UnlockWithSeed: {
        QTextEdit *seedEdit = findChild<QTextEdit*>("seedEdit");
        if (seedEdit) tryUnlockWithSeed(seedEdit->toPlainText());
        } break;

    case UnlockStaking:
    case Unlock: {
        bool stakingOnly = ui->stakingCheckBox->isChecked();
        if(!model->setWalletLocked(false, oldpass, stakingOnly)) {
            QMessageBox::critical(this, tr("Wallet unlock failed"),
                tr("The passphrase entered for the wallet decryption was incorrect."));
        } else {
            if(stakingOnly) {
                QMessageBox::information(this, tr("Unlocked Staking Only"),
                    tr("Wallet has been unlocked for Staking Only!"));
                fWalletUnlockStakingOnly = true;
            }
            QDialog::accept();
        }
        } break;

    case Decrypt:
        if(!model->setWalletEncrypted(false, oldpass)) {
            QMessageBox::critical(this, tr("Wallet decryption failed"),
                tr("The passphrase entered for the wallet decryption was incorrect."));
        } else {
            QMessageBox::information(this, tr("Wallet decrypted"),
                tr("Wallet successfully decrypted.\n\n"
                   "Your wallet is no longer password protected.\n"
                   "You can re-encrypt it via Settings \u2192 Encrypt Wallet "
                   "to generate a 24-word recovery phrase."));
            QDialog::accept();
        }
        break;

    case ChangePass:
        if(newpass1 == newpass2) {
            if(model->changePassphrase(oldpass, newpass1)) {
                // Update mnemonic master key to match new password
                if(model->hasMnemonicMasterKey()) {
                    model->removeMnemonicMasterKey();
                    // Unlock with new password to add new mnemonic key
                    model->setWalletLocked(false, newpass1);
                    model->addMnemonicMasterKey(newpass1);
                    model->setWalletLocked(true);
                }
                QMessageBox::information(this, tr("Wallet encrypted"),
                    tr("Wallet passphrase was successfully changed.\n"
                       "Your recovery phrase has been updated to match your new password."));
                QDialog::accept();
            } else {
                QMessageBox::critical(this, tr("Wallet encryption failed"),
                    tr("The passphrase entered for the wallet decryption was incorrect."));
            }
        } else {
            QMessageBox::critical(this, tr("Wallet encryption failed"),
                tr("The supplied passphrases do not match."));
        }
        break;
    }
}

// ── textChanged ───────────────────────────────────────────────────────────────

void AskPassphraseDialog::textChanged()
{
    bool acceptable = false;
    switch(mode)
    {
    case Encrypt:
        acceptable = !ui->passEdit2->text().isEmpty() && !ui->passEdit3->text().isEmpty();
        break;
    case UnlockWithSeed:
        acceptable = true;
        break;
    case UnlockStaking:
    case Unlock:
    case Decrypt:
        acceptable = !ui->passEdit1->text().isEmpty();
        break;
    case ChangePass:
        acceptable = !ui->passEdit1->text().isEmpty() &&
                     !ui->passEdit2->text().isEmpty() &&
                     !ui->passEdit3->text().isEmpty();
        break;
    }
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(acceptable);
}

// ── Event handlers ────────────────────────────────────────────────────────────

bool AskPassphraseDialog::event(QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_CapsLock) {
            fCapsLock = !fCapsLock;
        }
        if (fCapsLock) {
            ui->capsLabel->setText(tr("Warning: The Caps Lock key is on!"));
        } else {
            ui->capsLabel->clear();
        }
    }
    return QWidget::event(event);
}

bool AskPassphraseDialog::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        QString str = ke->text();
        if (str.length() != 0) {
            const QChar *psz = str.unicode();
            bool fShift = (ke->modifiers() & Qt::ShiftModifier) != 0;
            if ((fShift && psz->isLower()) || (!fShift && psz->isUpper())) {
                fCapsLock = true;
                ui->capsLabel->setText(tr("Warning: The Caps Lock key is on!"));
            } else if (psz->isLetter()) {
                fCapsLock = false;
                ui->capsLabel->clear();
            }
        }
    }
    return QDialog::eventFilter(object, event);
}

void AskPassphraseDialog::secureClearPassFields()
{
    ui->passEdit1->setText(QString(" ").repeated(ui->passEdit1->text().size()));
    ui->passEdit2->setText(QString(" ").repeated(ui->passEdit2->text().size()));
    ui->passEdit3->setText(QString(" ").repeated(ui->passEdit3->text().size()));
    ui->passEdit1->clear();
    ui->passEdit2->clear();
    ui->passEdit3->clear();
}
