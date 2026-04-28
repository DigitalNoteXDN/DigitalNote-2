// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// bip39_passphrase.h
// Passphrase <-> BIP39 mnemonic derivation for wallet password recovery.

#pragma once

#include <bip39/bip39_wallet.h>  // SecureString, Result

namespace BIP39Passphrase {

// Result codes — mirrors BIP39Wallet::Result for consistency
using Result = BIP39Wallet::Result;

// Derive a 24-word BIP39 mnemonic from a wallet passphrase.
// Uses PBKDF2-HMAC-SHA512 with fixed salt — deterministic and reversible.
// Returns Result::OK on success.
Result mnemonicFromPassphrase(const SecureString& passphrase,
                               SecureString& mnemonic);

// Recover the wallet passphrase (64-char hex) from a 24-word mnemonic.
// Returns Result::OK on success, ERR_MNEMONIC_INVALID if words are wrong.
Result passphraseFromMnemonic(const SecureString& mnemonic,
                               SecureString& passphrase);

} // namespace BIP39Passphrase
