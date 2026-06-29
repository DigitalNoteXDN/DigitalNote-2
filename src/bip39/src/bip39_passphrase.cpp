// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// bip39_passphrase.cpp
// BIP39 mnemonic derivation for wallet recovery.
//
// See bip39_passphrase.h for the design rationale (D1 password-derived
// vs D2 vMasterKey-derived).
//
// Now compiled directly into the wallet binary, so SecureString and
// secure_allocator are available naturally via the wallet headers.

#include <bip39/bip39_passphrase.h>
#include <bip39/entropy.h>
#include <bip39/checksum.h>
#include <bip39/mnemonic.h>
#include "allocators/secure_allocator.h"

#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <cstring>
#include <vector>

namespace BIP39Passphrase {

// ---------------------------------------------------------------------------
// Salt strings -- DIFFERENT for D1 vs D2 so the two derivations cannot
// produce the same mnemonic for some adversarial input.  D2 will be the
// only path used in v2.0.0.7+, but keeping them disjoint costs nothing.
// ---------------------------------------------------------------------------
static const char* XDN_RECOVERY_SALT_D1   = "XDN-wallet-recovery-v1";
static const char* XDN_RECOVERY_SALT_D2   = "XDN-vmasterkey-recovery-v2";
static const int   XDN_RECOVERY_ITERS_D1  = 100000;
static const int   XDN_RECOVERY_ITERS_D2  = 100000;
static const int   XDN_RECOVERY_BYTES     = 32;  // 256 bits -> 24-word mnemonic

// Internal helper: take 32 bytes of entropy, produce a 24-word mnemonic.
// Both D1 and D2 funnel into this once they have their 32 bytes.
static Result entropyBytesToMnemonic(std::vector<unsigned char>& entropyBytes,
                                     SecureString& mnemonic)
{
    mnemonic.clear();

    if (entropyBytes.size() != XDN_RECOVERY_BYTES) {
        OPENSSL_cleanse(entropyBytes.data(), entropyBytes.size());
        return Result::ERR_INTERNAL;
    }

    try {
        BIP39::Data entropyData(entropyBytes.begin(), entropyBytes.end());
        OPENSSL_cleanse(entropyBytes.data(), entropyBytes.size());

        BIP39::Entropy ent(entropyData);
        BIP39::CheckSum cs;
        if (!ent.genCheckSum(cs)) return Result::ERR_OPENSSL;

        BIP39::Mnemonic mn;
        if (!mn.LoadLanguage("EN")) return Result::ERR_INTERNAL;
        if (!mn.Set(ent, cs))       return Result::ERR_INTERNAL;

        std::string words = mn.GetStr();
        mnemonic.assign(words.begin(), words.end());
        OPENSSL_cleanse(const_cast<char*>(words.data()), words.size());
        return mnemonic.empty() ? Result::ERR_INTERNAL : Result::OK;

    } catch (...) {
        OPENSSL_cleanse(entropyBytes.data(), entropyBytes.size());
        return Result::ERR_INTERNAL;
    }
}

// ---------------------------------------------------------------------------
// D2 -- vMasterKey -> 24-word mnemonic
// ---------------------------------------------------------------------------
Result mnemonicFromVMasterKey(const CKeyingMaterial& vMasterKey,
                              SecureString& mnemonic)
{
    mnemonic.clear();

    if (vMasterKey.size() < 32) {
        return Result::ERR_INTERNAL;
    }

    // PBKDF2-HMAC-SHA512: vMasterKey -> 32 entropy bytes
    std::vector<unsigned char> entropyBytes(XDN_RECOVERY_BYTES);
    int rc = PKCS5_PBKDF2_HMAC(
        reinterpret_cast<const char*>(vMasterKey.data()),
        static_cast<int>(vMasterKey.size()),
        reinterpret_cast<const unsigned char*>(XDN_RECOVERY_SALT_D2),
        static_cast<int>(strlen(XDN_RECOVERY_SALT_D2)),
        XDN_RECOVERY_ITERS_D2,
        EVP_sha512(),
        XDN_RECOVERY_BYTES,
        entropyBytes.data());

    if (rc != 1) {
        OPENSSL_cleanse(entropyBytes.data(), entropyBytes.size());
        return Result::ERR_OPENSSL;
    }

    return entropyBytesToMnemonic(entropyBytes, mnemonic);
}

// ---------------------------------------------------------------------------
// D1 -- passphrase -> 24-word mnemonic   (legacy)
// ---------------------------------------------------------------------------
Result mnemonicFromPassphrase(const SecureString& passphrase,
                               SecureString& mnemonic)
{
    mnemonic.clear();
    if (passphrase.empty()) return Result::ERR_INTERNAL;

    // PBKDF2-HMAC-SHA512: passphrase -> 32 entropy bytes
    std::vector<unsigned char> entropyBytes(XDN_RECOVERY_BYTES);
    int rc = PKCS5_PBKDF2_HMAC(
        passphrase.data(), static_cast<int>(passphrase.size()),
        reinterpret_cast<const unsigned char*>(XDN_RECOVERY_SALT_D1),
        static_cast<int>(strlen(XDN_RECOVERY_SALT_D1)),
        XDN_RECOVERY_ITERS_D1,
        EVP_sha512(),
        XDN_RECOVERY_BYTES,
        entropyBytes.data());

    if (rc != 1) {
        OPENSSL_cleanse(entropyBytes.data(), entropyBytes.size());
        return Result::ERR_OPENSSL;
    }

    return entropyBytesToMnemonic(entropyBytes, mnemonic);
}

// ---------------------------------------------------------------------------
// Shared -- mnemonic -> 64-char hex (32 raw bytes)
// ---------------------------------------------------------------------------
Result passphraseFromMnemonic(const SecureString& mnemonic,
                               SecureString& passphrase)
{
    passphrase.clear();
    if (mnemonic.empty()) return Result::ERR_MNEMONIC_INVALID;

    try {
        std::string words(mnemonic.begin(), mnemonic.end());

        BIP39::Mnemonic mn;
        if (!mn.LoadLanguage("EN")) return Result::ERR_INTERNAL;
        if (!mn.Set(words))         return Result::ERR_MNEMONIC_INVALID;

        const BIP39::Entropy& ent = mn.GetEntropy();

        static const char* hexChars = "0123456789abcdef";
        SecureString hexPass;
        hexPass.reserve(XDN_RECOVERY_BYTES * 2);
        for (unsigned int i = 0; i < ent.size() && (int)i < XDN_RECOVERY_BYTES; ++i) {
            hexPass += hexChars[(ent[i] >> 4) & 0xf];
            hexPass += hexChars[ ent[i]       & 0xf];
        }

        if ((int)hexPass.size() != XDN_RECOVERY_BYTES * 2) return Result::ERR_INTERNAL;
        passphrase = hexPass;
        OPENSSL_cleanse(const_cast<char*>(hexPass.data()), hexPass.size());
        OPENSSL_cleanse(const_cast<char*>(words.data()), words.size());
        return Result::OK;

    } catch (...) {
        return Result::ERR_INTERNAL;
    }
}

} // namespace BIP39Passphrase
