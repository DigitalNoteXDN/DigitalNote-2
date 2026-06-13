// Copyright (c) 2024-2025 DigitalNote XDN developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// bip39_wallet.cpp -- Bridge between CWallet and BIP39-Mnemonic library.
//
// DigitalNote-2 uses a traditional JBOK (Just a Bunch of Keys) wallet with no
// HD chain. The BIP39 mnemonic is derived from the wallet master encryption key
// (vMasterKey), which is a 32-byte random value that is the root secret of the
// encrypted wallet. The wallet must be unlocked to access this value.

#include <bip39/bip39_wallet.h>

// BIP39-Mnemonic library headers (actual submodule API)
#include <bip39.h>
#include <bip39/entropy.h>
#include <bip39/checksum.h>
#include <bip39/mnemonic.h>
#include <bip39/seed.h>

// DigitalNote-2 wallet headers
#include "cwallet.h"
#include "ccryptokeystore.h"
#include "thread.h"       // LOCK
#include "../../util.h"   // DigitalNote-2 util.h (LogPrintf)

#include <openssl/crypto.h>
#include <algorithm>

namespace BIP39Wallet {

// ---- Helpers ----------------------------------------------------------------

const char* resultToString(Result r) noexcept
{
    switch (r) {
    case Result::OK:                    return "Success";
    case Result::ERR_WALLET_LOCKED:     return "Wallet is locked -- please enter your passphrase";
    case Result::ERR_NO_HD_SEED:        return "No wallet master key found -- wallet may be unencrypted";
    case Result::ERR_ENTROPY_TOO_SHORT: return "Wallet master key is shorter than the requested mnemonic entropy";
    case Result::ERR_MNEMONIC_INVALID:  return "Mnemonic is invalid (checksum or word-list error)";
    case Result::ERR_OPENSSL:           return "OpenSSL cryptographic error";
    case Result::ERR_INTERNAL:          return "Unexpected internal error";
    }
    return "Unknown error";
}

static int entropyBytes(WordCount wc) noexcept
{
    return entropyBits(wc) / 8;
}

// ---- generateMnemonic -------------------------------------------------------

Result generateMnemonic(const CWallet& wallet,
                        WordCount wordCount,
                        SecureString& mnemonic)
{
    mnemonic.clear();

    // Wallet must be encrypted and unlocked to access vMasterKey
    if (!wallet.IsCrypted())
        return Result::ERR_NO_HD_SEED;

    if (wallet.IsLocked())
        return Result::ERR_WALLET_LOCKED;

    const int needed = entropyBytes(wordCount);

    // Access vMasterKey — it is protected member of CCryptoKeyStore
    // which CWallet inherits from. We read it while holding cs_wallet.
    CKeyingMaterial entropyData;
    {
        LOCK(wallet.cs_wallet);
        const CKeyingMaterial& mk = wallet.vMasterKey;
        if (static_cast<int>(mk.size()) < needed)
            return Result::ERR_ENTROPY_TOO_SHORT;
        entropyData.assign(mk.begin(), mk.begin() + needed);
    }

    try {
        BIP39::Data rawEntropy(entropyData.begin(), entropyData.end());
        OPENSSL_cleanse(entropyData.data(), entropyData.size());

        BIP39::Entropy ent(rawEntropy);
        OPENSSL_cleanse(rawEntropy.data(), rawEntropy.size());

        BIP39::CheckSum cs;
        if (!ent.genCheckSum(cs))
            return Result::ERR_OPENSSL;

        BIP39::Mnemonic mn;
        if (!mn.LoadLanguage("EN"))
            return Result::ERR_INTERNAL;

        if (!mn.Set(ent, cs))
            return Result::ERR_INTERNAL;

        const std::string words = mn.GetStr();
        mnemonic.assign(words.begin(), words.end());

        return Result::OK;

    } catch (const std::exception& e) {
        LogPrintf("BIP39Wallet::generateMnemonic: exception: %s\n", e.what());
        return Result::ERR_INTERNAL;
    }
}

// ---- validateMnemonic -------------------------------------------------------

bool validateMnemonic(const SecureString& mnemonic)
{
    try {
        std::string words(mnemonic.begin(), mnemonic.end());

        BIP39::Mnemonic mn;
        if (!mn.LoadLanguage("EN"))
            return false;

        return mn.Set(words);

    } catch (...) {
        return false;
    }
}

// ---- restoreFromMnemonic ----------------------------------------------------
// Note: restoreFromMnemonic cannot be implemented for a JBOK wallet because
// there is no HD seed path to restore. This function validates the mnemonic
// but returns ERR_INTERNAL to indicate it is not supported.

Result restoreFromMnemonic(CWallet& wallet,
                           const SecureString& mnemonic,
                           const SecureString& passphrase)
{
    (void)wallet;
    (void)passphrase;

    if (!validateMnemonic(mnemonic))
        return Result::ERR_MNEMONIC_INVALID;

    // Restore-from-mnemonic is not supported for non-HD wallets.
    // The mnemonic is display-only (derived from vMasterKey).
    LogPrintf("BIP39Wallet::restoreFromMnemonic: not supported for non-HD wallets\n");
    return Result::ERR_INTERNAL;
}

} // namespace BIP39Wallet