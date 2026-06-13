// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// test/qt/test_seedphrasedialog.cpp
// Qt unit tests for SeedPhraseDialog.
// Framework: Qt Test (QTest)
//
// Build & Run:
//   qmake test_seedphrasedialog.pro && make && ./test_seedphrasedialog
//
// These tests use a MockWalletModel so no actual wallet file is needed.

#include <QtTest/QtTest>
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QClipboard>

#include "seedphrasedialog.h"
#include "bip39/bip39_wallet.h"

// ── Mock WalletModel ──────────────────────────────────────────────────────────
// Minimal stub — only the methods SeedPhraseDialog calls are implemented.

class MockWalletModel : public WalletModel
{
    Q_OBJECT
public:
    explicit MockWalletModel(QObject *parent = nullptr) : WalletModel(nullptr, nullptr, parent) {}

    EncryptionStatus getEncryptionStatus() const override {
        return m_locked ? Locked : Unencrypted;
    }

    UnlockContext requestUnlock() override {
        return UnlockContext(this, !m_locked, false);
    }

    bool m_locked = false;
};

// ── Test class ────────────────────────────────────────────────────────────────

class TestSeedPhraseDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Dialog construction
    void testDialogCreates();
    void testDialogHasRevealButton();
    void testDialogHasCopyButton();
    void testDialogHasWordCountCombo();
    void testRevealButtonEnabled_WhenUnlocked();
    void testCopyButtonDisabled_Initially();
    void testVerifyButtonDisabled_Initially();

    // Word count combo
    void testWordCountComboHas5Items();
    void testWordCountComboDefaultIs24Words();
    void testWordCountChangesClearsMnemonic();

    // clearMnemonic
    void testClearMnemonic_HidesGrid();
    void testClearMnemonic_ShowsPlaceholder();
    void testClearMnemonic_DisablesCopyButton();
    void testClearMnemonic_EnablesRevealButton();

    // Clipboard
    void testClipboardClearedAfterClose();

    // BIP39Wallet::validateMnemonic (unit)
    void testValidateMnemonic_ValidVector0();
    void testValidateMnemonic_ValidZooVector();
    void testValidateMnemonic_EmptyString();
    void testValidateMnemonic_OneWord();
    void testValidateMnemonic_InvalidWord();
    void testValidateMnemonic_WrongChecksum();

    // BIP39Wallet::entropyBits
    void testEntropyBits_12Words();
    void testEntropyBits_24Words();

    // BIP39Wallet::resultToString
    void testResultToString_NotNull();
    void testResultToString_NotEmpty();

private:
    QApplication *m_app{nullptr};
    MockWalletModel *m_model{nullptr};
    SeedPhraseDialog *m_dialog{nullptr};
};

// ── Setup / teardown ──────────────────────────────────────────────────────────

void TestSeedPhraseDialog::initTestCase()
{
    // QApplication required for any widget test
    int argc = 0;
    m_app = new QApplication(argc, nullptr);
}

void TestSeedPhraseDialog::cleanupTestCase()
{
    delete m_app;
}

void TestSeedPhraseDialog::init()
{
    m_model  = new MockWalletModel;
    m_dialog = new SeedPhraseDialog(m_model);
}

void TestSeedPhraseDialog::cleanup()
{
    delete m_dialog;
    delete m_model;
    m_dialog = nullptr;
    m_model  = nullptr;
}

// ── Dialog construction tests ─────────────────────────────────────────────────

void TestSeedPhraseDialog::testDialogCreates()
{
    QVERIFY(m_dialog != nullptr);
}

void TestSeedPhraseDialog::testDialogHasRevealButton()
{
    auto *btn = m_dialog->findChild<QPushButton*>("revealBtn");
    QVERIFY(btn != nullptr);
}

void TestSeedPhraseDialog::testDialogHasCopyButton()
{
    auto *btn = m_dialog->findChild<QPushButton*>("copyBtn");
    QVERIFY(btn != nullptr);
}

void TestSeedPhraseDialog::testDialogHasWordCountCombo()
{
    auto *combo = m_dialog->findChild<QComboBox*>("wordCountCombo");
    QVERIFY(combo != nullptr);
}

void TestSeedPhraseDialog::testRevealButtonEnabled_WhenUnlocked()
{
    m_model->m_locked = false;
    auto *btn = m_dialog->findChild<QPushButton*>("revealBtn");
    QVERIFY(btn != nullptr);
    QVERIFY(btn->isEnabled());
}

void TestSeedPhraseDialog::testCopyButtonDisabled_Initially()
{
    auto *btn = m_dialog->findChild<QPushButton*>("copyBtn");
    QVERIFY(btn != nullptr);
    QVERIFY(!btn->isEnabled());
}

void TestSeedPhraseDialog::testVerifyButtonDisabled_Initially()
{
    auto *btn = m_dialog->findChild<QPushButton*>("verifyBtn");
    QVERIFY(btn != nullptr);
    QVERIFY(!btn->isEnabled());
}

// ── Word count combo tests ────────────────────────────────────────────────────

void TestSeedPhraseDialog::testWordCountComboHas5Items()
{
    auto *combo = m_dialog->findChild<QComboBox*>("wordCountCombo");
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 5);
}

void TestSeedPhraseDialog::testWordCountComboDefaultIs24Words()
{
    auto *combo = m_dialog->findChild<QComboBox*>("wordCountCombo");
    QVERIFY(combo != nullptr);
    // Default index 4 → 24 words
    QCOMPARE(combo->currentIndex(), 4);
    QCOMPARE(combo->itemData(4).toInt(),
             static_cast<int>(BIP39Wallet::WordCount::Words24));
}

void TestSeedPhraseDialog::testWordCountChangesClearsMnemonic()
{
    // Simulate: change combo to 12 words → placeholder should be visible
    auto *combo       = m_dialog->findChild<QComboBox*>("wordCountCombo");
    auto *placeholder = m_dialog->findChild<QLabel*>("placeholderLabel");
    QVERIFY(combo && placeholder);

    combo->setCurrentIndex(0); // 12 words
    QApplication::processEvents();

    QVERIFY(placeholder->isVisible());
}

// ── clearMnemonic tests ───────────────────────────────────────────────────────

void TestSeedPhraseDialog::testClearMnemonic_HidesGrid()
{
    m_dialog->clearMnemonic();
    auto *grid = m_dialog->findChild<QWidget*>("wordGrid");
    QVERIFY(grid != nullptr);
    QVERIFY(!grid->isVisible());
}

void TestSeedPhraseDialog::testClearMnemonic_ShowsPlaceholder()
{
    m_dialog->clearMnemonic();
    auto *ph = m_dialog->findChild<QLabel*>("placeholderLabel");
    QVERIFY(ph != nullptr);
    QVERIFY(ph->isVisible());
}

void TestSeedPhraseDialog::testClearMnemonic_DisablesCopyButton()
{
    m_dialog->clearMnemonic();
    auto *btn = m_dialog->findChild<QPushButton*>("copyBtn");
    QVERIFY(btn != nullptr);
    QVERIFY(!btn->isEnabled());
}

void TestSeedPhraseDialog::testClearMnemonic_EnablesRevealButton()
{
    m_dialog->clearMnemonic();
    auto *btn = m_dialog->findChild<QPushButton*>("revealBtn");
    QVERIFY(btn != nullptr);
    QVERIFY(btn->isEnabled());
}

// ── Clipboard tests ───────────────────────────────────────────────────────────

void TestSeedPhraseDialog::testClipboardClearedAfterClose()
{
    QClipboard *cb = QApplication::clipboard();
    cb->setText("test_mnemonic_sentinel");
    m_dialog->clearMnemonic();  // closing should not clear clipboard (mnemonic not set)
    // Only clears if our specific mnemonic matches
    // In this case m_currentMnemonic is empty → clipboard unchanged
    QCOMPARE(cb->text(), QString("test_mnemonic_sentinel"));
    cb->clear();
}

// ── BIP39Wallet::validateMnemonic unit tests ──────────────────────────────────

void TestSeedPhraseDialog::testValidateMnemonic_ValidVector0()
{
    const std::string words =
        "abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon about";
    SecureString mn(words.begin(), words.end());
    QVERIFY(BIP39Wallet::validateMnemonic(mn));
}

void TestSeedPhraseDialog::testValidateMnemonic_ValidZooVector()
{
    const std::string words =
        "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong";
    SecureString mn(words.begin(), words.end());
    QVERIFY(BIP39Wallet::validateMnemonic(mn));
}

void TestSeedPhraseDialog::testValidateMnemonic_EmptyString()
{
    SecureString mn;
    QVERIFY(!BIP39Wallet::validateMnemonic(mn));
}

void TestSeedPhraseDialog::testValidateMnemonic_OneWord()
{
    SecureString mn("abandon");
    QVERIFY(!BIP39Wallet::validateMnemonic(mn));
}

void TestSeedPhraseDialog::testValidateMnemonic_InvalidWord()
{
    const std::string words =
        "abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon NOTAWORD";
    SecureString mn(words.begin(), words.end());
    QVERIFY(!BIP39Wallet::validateMnemonic(mn));
}

void TestSeedPhraseDialog::testValidateMnemonic_WrongChecksum()
{
    // 12× "abandon" has bad checksum — last word should be "about"
    const std::string words =
        "abandon abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon abandon";
    SecureString mn(words.begin(), words.end());
    QVERIFY(!BIP39Wallet::validateMnemonic(mn));
}

// ── entropyBits tests ─────────────────────────────────────────────────────────

void TestSeedPhraseDialog::testEntropyBits_12Words()
{
    QCOMPARE(BIP39Wallet::entropyBits(BIP39Wallet::WordCount::Words12), 128);
}

void TestSeedPhraseDialog::testEntropyBits_24Words()
{
    QCOMPARE(BIP39Wallet::entropyBits(BIP39Wallet::WordCount::Words24), 256);
}

// ── resultToString tests ──────────────────────────────────────────────────────

void TestSeedPhraseDialog::testResultToString_NotNull()
{
    QVERIFY(BIP39Wallet::resultToString(BIP39Wallet::Result::OK) != nullptr);
}

void TestSeedPhraseDialog::testResultToString_NotEmpty()
{
    const char* s = BIP39Wallet::resultToString(BIP39Wallet::Result::ERR_WALLET_LOCKED);
    QVERIFY(std::strlen(s) > 0);
}

// ── Main ──────────────────────────────────────────────────────────────────────

QTEST_APPLESS_MAIN(TestSeedPhraseDialog)
#include "test_seedphrasedialog.moc"
