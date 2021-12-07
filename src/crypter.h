// Copyright (c) 2009-2012 The Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef CRYPTER_H
#define CRYPTER_H

#include <string>
#include <vector>

#include "allocators/securestring.h"
#include "types/ckeyingmaterial.h"

class uint256;

const unsigned int WALLET_CRYPTO_KEY_SIZE = 32;
const unsigned int WALLET_CRYPTO_SALT_SIZE = 8;

bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial &vchPlaintext,
		const uint256& nIV, std::vector<unsigned char> &vchCiphertext);
bool DecryptSecret(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCiphertext,
		const uint256& nIV, CKeyingMaterial& vchPlaintext);
bool EncryptAES256(const SecureString& sKey, const SecureString& sPlaintext,
		const std::string& sIV, std::string& sCiphertext);
bool DecryptAES256(const SecureString& sKey, const std::string& sCiphertext,
		const std::string& sIV, SecureString& sPlaintext);

#endif // CRYPTER_H
