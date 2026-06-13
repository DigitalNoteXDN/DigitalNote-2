#ifndef ASKPASSPHRASEDIALOG_H
#define ASKPASSPHRASEDIALOG_H

#include <QDialog>
#include <QString>

namespace Ui {
    class AskPassphraseDialog;
}
class WalletModel;

/** Multifunctional dialog to ask for passphrases. Used for encryption, unlocking, and changing the passphrase.
 */
class AskPassphraseDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        Encrypt,            /**< Ask passphrase twice and encrypt */
        UnlockStaking,      /**< Ask passphrase and unlock for staking */
        Unlock,             /**< Ask passphrase and unlock */
        UnlockWithSeed,     /**< Ask recovery phrase and unlock */
        ChangePass,         /**< Ask old passphrase + new passphrase twice */
        Decrypt             /**< Ask passphrase and decrypt wallet */
    };

    explicit AskPassphraseDialog(Mode mode, QWidget *parent = 0);
    ~AskPassphraseDialog();

    void accept();

    void setModel(WalletModel *model);

private:
    Ui::AskPassphraseDialog *ui;
    Mode mode;
    WalletModel *model;
    bool fCapsLock;

    void setupEncryptMode();

private slots:
    void textChanged();
    void onGeneratePassword();
    void onSwitchToPassword();
    void onSwitchToSeed();
    void tryUnlockWithSeed(const QString& seedPhrase);

protected:
    bool event(QEvent *event);
    bool eventFilter(QObject *, QEvent *event);
    void secureClearPassFields();
};

#endif // ASKPASSPHRASEDIALOG_H
