// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// src/qt/test/walletmodel_tests.cpp
//
// Qt unit tests for WalletModel, AddressTableModel, and
// TransactionTableModel — the existing GUI data models.
// These sit in src/qt/test/ matching Bitcoin Core convention and are
// compiled into test_digitalnote-qt by bitcoin-qt.pro.
//
// Framework: Qt Test (QTest)
// Run: ./test_digitalnote-qt -v2

#include <QtTest/QtTest>
#include <QApplication>
#include <QString>
#include <QStringList>
#include <QAbstractItemModel>

// Qt wallet model headers
#include "walletmodel.h"
#include "addresstablemodel.h"
#include "transactiontablemodel.h"
#include "optionsmodel.h"
#include "clientmodel.h"

// Core headers for type checking
#include "amount.h"
#include "clientversion.h"
#include "version.h"

class TestWalletModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ── Version checks visible through ClientModel ───────────────────────────
    void testClientVersionString();
    void testProtocolVersionFromModel();

    // ── OptionsModel ─────────────────────────────────────────────────────────
    void testOptionsModelCreates();
    void testDisplayUnitDefaultIsXDN();
    void testDisplayUnitLabels();

    // ── WalletModel encryption status ────────────────────────────────────────
    void testEncryptionStatusEnum();

    // ── AddressTableModel columns ─────────────────────────────────────────────
    void testAddressTableColumnCount();
    void testAddressTableColumnHeaders();

    // ── TransactionTableModel columns ─────────────────────────────────────────
    void testTransactionTableColumnCount();
    void testTransactionTableColumnHeaders();

    // ── SendCoinsRecipient ────────────────────────────────────────────────────
    void testSendCoinsRecipientDefaults();
    void testSendCoinsRecipientAmountValidation();

    // ── AmountFormatting via WalletModel statics ──────────────────────────────
    void testFormatAmountOneXDN();
    void testFormatAmountZero();
    void testFormatAmountMaxMoney();

private:
    QApplication *m_app{nullptr};
};

// ── Setup ─────────────────────────────────────────────────────────────────────

void TestWalletModel::initTestCase()
{
    int argc = 0;
    m_app = new QApplication(argc, nullptr);
}

void TestWalletModel::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

// ── Version ───────────────────────────────────────────────────────────────────

void TestWalletModel::testClientVersionString()
{
    // FormatFullVersion() should contain "2.0.0.7"
    QString ver = QString::fromStdString(FormatFullVersion());
    QVERIFY(!ver.isEmpty());
    QVERIFY2(ver.contains("2.0.0.7"),
             qPrintable("Expected '2.0.0.7' in version string, got: " + ver));
}

void TestWalletModel::testProtocolVersionFromModel()
{
    // PROTOCOL_VERSION constant must equal 2007
    QCOMPARE(PROTOCOL_VERSION, 2007);
}

// ── OptionsModel ──────────────────────────────────────────────────────────────

void TestWalletModel::testOptionsModelCreates()
{
    OptionsModel model;
    // Just verify it constructs without crashing
    QVERIFY(true);
}

void TestWalletModel::testDisplayUnitDefaultIsXDN()
{
    OptionsModel model;
    // Default display unit should be XDN (0 in BitcoinUnits)
    int unit = model.getDisplayUnit();
    QVERIFY(unit >= 0);
    // BitcoinUnits::name(unit) should contain "XDN"
    QString name = BitcoinUnits::name(unit);
    QVERIFY2(name.contains("XDN"),
             qPrintable("Expected display unit name to contain 'XDN', got: " + name));
}

void TestWalletModel::testDisplayUnitLabels()
{
    // All DigitalNote display units should have non-empty names
    for (BitcoinUnits::Unit u : BitcoinUnits::availableUnits()) {
        QVERIFY(!BitcoinUnits::name(u).isEmpty());
        QVERIFY(!BitcoinUnits::longName(u).isEmpty());
    }
}

// ── WalletModel encryption status ────────────────────────────────────────────

void TestWalletModel::testEncryptionStatusEnum()
{
    // Verify enum values are distinct — used in GUI state machine
    QVERIFY(WalletModel::Unencrypted != WalletModel::Locked);
    QVERIFY(WalletModel::Locked      != WalletModel::Unlocked);
    QVERIFY(WalletModel::Unencrypted != WalletModel::Unlocked);
}

// ── AddressTableModel ─────────────────────────────────────────────────────────

void TestWalletModel::testAddressTableColumnCount()
{
    // AddressTableModel has Label and Address columns
    QCOMPARE(AddressTableModel::ColumnIndex::Label,   0);
    QCOMPARE(AddressTableModel::ColumnIndex::Address, 1);
}

void TestWalletModel::testAddressTableColumnHeaders()
{
    // The column count enum values are 0 and 1 — exactly 2 columns
    int numCols = AddressTableModel::ColumnIndex::Address + 1;
    QCOMPARE(numCols, 2);
}

// ── TransactionTableModel ─────────────────────────────────────────────────────

void TestWalletModel::testTransactionTableColumnCount()
{
    // TransactionTableModel::ColumnIndex should have at least Status, Date,
    // Type, ToAddress, Amount
    QVERIFY(TransactionTableModel::Status   >= 0);
    QVERIFY(TransactionTableModel::Date     >= 0);
    QVERIFY(TransactionTableModel::Type     >= 0);
    QVERIFY(TransactionTableModel::ToAddress >= 0);
    QVERIFY(TransactionTableModel::Amount   >= 0);
    // All distinct
    QVERIFY(TransactionTableModel::Status != TransactionTableModel::Date);
    QVERIFY(TransactionTableModel::Date   != TransactionTableModel::Amount);
}

void TestWalletModel::testTransactionTableColumnHeaders()
{
    // Column count should be at least 5
    QVERIFY(TransactionTableModel::Amount + 1 >= 5);
}

// ── SendCoinsRecipient ────────────────────────────────────────────────────────

void TestWalletModel::testSendCoinsRecipientDefaults()
{
    SendCoinsRecipient r;
    QVERIFY(r.address.isEmpty());
    QVERIFY(r.label.isEmpty());
    QCOMPARE(r.amount, CAmount(0));
}

void TestWalletModel::testSendCoinsRecipientAmountValidation()
{
    SendCoinsRecipient r;
    r.amount = COIN;
    QVERIFY(MoneyRange(r.amount));

    r.amount = MAX_MONEY + 1;
    QVERIFY(!MoneyRange(r.amount));
}

// ── Amount formatting (static helpers) ───────────────────────────────────────

void TestWalletModel::testFormatAmountOneXDN()
{
    // 1 XDN in the wallet's default unit
    QString s = BitcoinUnits::format(BitcoinUnits::BTC, COIN);
    QVERIFY(!s.isEmpty());
    QVERIFY(s.contains("1"));
}

void TestWalletModel::testFormatAmountZero()
{
    QString s = BitcoinUnits::format(BitcoinUnits::BTC, 0);
    QVERIFY(s.contains("0"));
}

void TestWalletModel::testFormatAmountMaxMoney()
{
    QString s = BitcoinUnits::format(BitcoinUnits::BTC, MAX_MONEY);
    QVERIFY(!s.isEmpty());
}

QTEST_MAIN(TestWalletModel)
#include "walletmodel_tests.moc"
