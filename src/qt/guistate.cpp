// Copyright (c) 2024-2026 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// guistate.cpp
// See header for design notes.

#include "guistate.h"

#include <QSettings>
#include <QCryptographicHash>
#include <QByteArray>
#include <QString>

namespace {

// Build the QSettings key for a per-wallet flag.
//
// QSettings interprets '/' as a group separator, and Windows registry
// values dislike backslashes and colons -- both of which appear in
// wallet paths.  Hashing first sidesteps all of that and gives us
// short, fixed-length, ASCII-safe keys.
//
// 16 hex chars = 64 bits of entropy.  The collision space is the set
// of wallet paths a single user has used over time -- realistically
// <100 -- so birthday-paradox risk is negligible.
QString perWalletKey(const QString& flagName, const std::string& walletPath)
{
    QByteArray pathBytes(walletPath.data(),
                         static_cast<int>(walletPath.size()));
    QByteArray digest = QCryptographicHash::hash(
        pathBytes, QCryptographicHash::Sha256);
    QString shortHash = QString::fromLatin1(digest.toHex().left(16));

    return QString("guiState/") + flagName + QString("/") + shortHash;
}

} // anonymous namespace

namespace GuiState {

bool isRecoveryPhraseUpgradeDeclined(const std::string& walletPath)
{
    QSettings settings;
    return settings.contains(
        perWalletKey("recoveryPhraseUpgradeDeclined", walletPath));
}

void setRecoveryPhraseUpgradeDeclined(const std::string& walletPath)
{
    QSettings settings;
    settings.setValue(
        perWalletKey("recoveryPhraseUpgradeDeclined", walletPath),
        true);
    // QSettings auto-syncs in its destructor, but force it now so the
    // dismissal survives a crash before the next sync interval.
    settings.sync();
}

bool is2MCollateralPromptSuppressed(const std::string& walletPath)
{
    QSettings settings;
    return settings.contains(
        perWalletKey("collateralPromptSuppressed", walletPath));
}

void set2MCollateralPromptSuppressed(const std::string& walletPath)
{
    QSettings settings;
    settings.setValue(
        perWalletKey("collateralPromptSuppressed", walletPath),
        true);
    settings.sync();
}

} // namespace GuiState