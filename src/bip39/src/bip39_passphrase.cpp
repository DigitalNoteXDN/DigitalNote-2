// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// bip39_passphrase.cpp
// Passphrase <-> BIP39 mnemonic derivation for wallet password recovery.
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

static const char* XDN_RECOVERY_SALT  = "XDN-wallet-recovery-v1";
static const int   XDN_RECOVERY_ITERS = 100000;
static const int   XDN_RECOVERY_BYTES = 32;  // 256 bits -> 24-word mnemonic

Result mnemonicFromPassphrase(const SecureString& passphrase,
                               SecureString& mnemonic)
{
    mnemonic.clear();
    if (passphrase.empty()) return Result::ERR_INTERNAL;

    // PBKDF2-HMAC-SHA512: passphrase -> 32 entropy bytes
    std::vector<unsigned char> entropyBytes(XDN_RECOVERY_BYTES);
    int rc = PKCS5_PBKDF2_HMAC(
        passphrase.data(), static_cast<int>(passphrase.size()),
        reinterpret_cast<const unsigned char*>(XDN_RECOVERY_SALT),
        static_cast<int>(strlen(XDN_RECOVERY_SALT)),
        XDN_RECOVERY_ITERS,
        EVP_sha512(),
        XDN_RECOVERY_BYTES,
        entropyBytes.data());

    if (rc != 1) {
        OPENSSL_cleanse(entropyBytes.data(), entropyBytes.size());
        return Result::ERR_OPENSSL;
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
