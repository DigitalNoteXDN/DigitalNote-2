// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// bip39_passphrase.h
// BIP39 mnemonic derivation for wallet recovery.
//
// This file supports two derivation paths:
//
//   D1 (legacy)   passphrase  -> PBKDF2 -> 32 bytes -> 24-word mnemonic
//                 The recovery phrase is tied to the user's password.
//                 Phrase changes whenever password changes.
//
//   D2 (current)  vMasterKey  -> HKDF-style PBKDF2 -> 32 bytes -> 24-word mnemonic
//                 The recovery phrase is tied to the wallet's master key,
//                 not the password.  Phrase is stable across password changes.
//
// D2 is the design used in DigitalNote v2.0.0.7 and later.  D1 functions
// remain compiled-in for any callers that still expect the old API surface
// but should NOT be used for new code.

#pragma once

#include <bip39/bip39_wallet.h>  // SecureString, Result
#include "types/ckeyingmaterial.h"  // CKeyingMaterial

namespace BIP39Passphrase {

// Result codes -- mirrors BIP39Wallet::Result for consistency
using Result = BIP39Wallet::Result;

// ---------------------------------------------------------------------------
// D2 -- vMasterKey-derived (recommended)
// ---------------------------------------------------------------------------

// Derive a 24-word BIP39 mnemonic from the wallet's vMasterKey.
// Uses PBKDF2-HMAC-SHA512 with a fixed, public salt -- deterministic.
//
// The wallet's vMasterKey is the canonical seed of trust; deriving the
// mnemonic from it (rather than from the user's password) means the
// recovery phrase is stable across password changes.
//
// vMasterKey is required to be at least 32 bytes (the standard wallet
// master-key size).  Returns ERR_INTERNAL if shorter.
Result mnemonicFromVMasterKey(const CKeyingMaterial& vMasterKey,
                              SecureString& mnemonic);

// ---------------------------------------------------------------------------
// D1 -- Password-derived (legacy, kept for compatibility / migration)
// ---------------------------------------------------------------------------

// Derive a 24-word BIP39 mnemonic from a wallet passphrase.
// Uses PBKDF2-HMAC-SHA512 with fixed salt -- deterministic and reversible.
// Returns Result::OK on success.
//
// DEPRECATED for new code.  Phrase is tied to the password, so it changes
// whenever the password changes.  Prefer mnemonicFromVMasterKey() instead.
Result mnemonicFromPassphrase(const SecureString& passphrase,
                              SecureString& mnemonic);

// ---------------------------------------------------------------------------
// Shared -- mnemonic -> 32 raw bytes (used by both D1 and D2 unlock paths)
// ---------------------------------------------------------------------------

// Recover the 32 raw entropy bytes from a 24-word mnemonic, encoded as a
// 64-character lowercase-hex SecureString (this hex string is then used as
// the AES key against CMasterKey[2]).
//
// The same function works for both D1 and D2: the mnemonic encodes 32 bytes
// of entropy regardless of how those bytes were originally derived.  The
// CWallet::Unlock() iterator simply tries the resulting hex against every
// stored CMasterKey envelope.
//
// Returns Result::OK on success, ERR_MNEMONIC_INVALID if words are wrong.
Result passphraseFromMnemonic(const SecureString& mnemonic,
                              SecureString& passphrase);

} // namespace BIP39Passphrase