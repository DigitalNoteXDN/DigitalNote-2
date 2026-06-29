// Copyright (c) 2024-2026 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// guistate.h
//
// Per-wallet GUI state preferences backed by QSettings.
//
// Why a separate file (and not wallet.dat):
//
//   * UI dismissals like "don't ask again" are user-interface preferences,
//     not wallet contents.  They don't belong inside a Berkeley DB file
//     that ships with cryptographic key material.
//   * QSettings is already the established pattern in this codebase for
//     UI preferences (see OptionsModel for fMinimizeToTray, nDisplayUnit,
//     etc.).
//   * wallet.dat records are at risk during -salvagewallet recovery on
//     corrupt wallets.  GUI preferences should not share that fate.
//
// Why per-wallet (and not a single global flag):
//
//   * A user with multiple wallets ($DATADIR/wallet.dat plus extras under
//     -wallet=) typically wants prompts to fire independently for each.
//     Dismissing the recovery-phrase upgrade prompt on wallet A should
//     not silently dismiss it on wallet B.
//   * Mechanism: each piece of state is keyed by a SHA-256 hash of the
//     absolute wallet path.  Presence of the QSettings key is the
//     "dismissed" signal -- no value semantics needed.
//
// Storage on disk:
//
//   Linux:   ~/.config/DigitalNote/DigitalNote-Qt.conf
//   macOS:   ~/Library/Preferences/com.DigitalNote.DigitalNote-Qt.plist
//   Windows: HKCU\Software\DigitalNote\DigitalNote-Qt
//
// Example resulting INI on Linux for two wallets:
//
//   [guiState]
//   recoveryPhraseUpgradeDeclined\a3f5b2c1d8e9f024 = true
//   recoveryPhraseUpgradeDeclined\7f2e8d4c1b6a9d50 = true

#ifndef GUISTATE_H
#define GUISTATE_H

#include <string>

namespace GuiState {

// ---------------------------------------------------------------------------
// Recovery phrase upgrade dismissal
// ---------------------------------------------------------------------------
//
// Set when the user clicks "Don't ask again for this wallet" on the
// recovery phrase upgrade prompt.  Stays set across wallet restarts but
// is per-wallet -- a different wallet file will be prompted afresh.
//
// walletPath: absolute path to the wallet file (typically
// (GetDataDir() / strWalletFile).string()).  The path is hashed before
// use as a QSettings key so filesystem characters (slashes, backslashes,
// colons on Windows) don't interact badly with QSettings group syntax.

bool isRecoveryPhraseUpgradeDeclined(const std::string& walletPath);
void setRecoveryPhraseUpgradeDeclined(const std::string& walletPath);

// ---------------------------------------------------------------------------
// Masternode collateral prompt suppression (B1)
// ---------------------------------------------------------------------------
//
// Set when the user clicks "Don't ask for this wallet" on the prompt
// that appears after receiving a 2,000,000 XDN UTXO (the masternode
// collateral amount).  Per-wallet: a different wallet file gets prompted
// afresh on the same kind of receive.
//
// Per-UTXO suppression is NOT needed -- the prompt fires from
// incomingTransaction which only fires once per UTXO appearance.  This
// flag is the user's blanket opt-out for a given wallet.

bool is2MCollateralPromptSuppressed(const std::string& walletPath);
void set2MCollateralPromptSuppressed(const std::string& walletPath);

} // namespace GuiState

#endif // GUISTATE_H