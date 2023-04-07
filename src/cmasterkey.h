#ifndef CMASTERKEY_H
#define CMASTERKEY_H

#include <vector>

/*
Private key encryption is done based on a CMasterKey,
which holds a salt and random encryption key.

CMasterKeys are encrypted using AES-256-CBC using a key
derived using derivation method nDerivationMethod
(0 == EVP_sha512()) and derivation iterations nDeriveIterations.
vchOtherDerivationParameters is provided for alternative algorithms
which may require more parameters (such as scrypt).

Wallet Private Keys are then encrypted using AES-256-CBC
with the double-sha256 of the public key as the IV, and the
master key's key as the encryption key (see keystore.[ch]).
*/

/** Master key for wallet encryption */
class CMasterKey
{
public:
	std::vector<unsigned char> vchCryptedKey;
	std::vector<unsigned char> vchSalt;

	// 0 = EVP_sha512()
	// 1 = scrypt()
	unsigned int nDerivationMethod;
	unsigned int nDeriveIterations;

	// Use this for more parameters to key derivation,
	// such as the various parameters to scrypt
	std::vector<unsigned char> vchOtherDerivationParameters;

	CMasterKey();
	CMasterKey(unsigned int nDerivationMethodIndex);

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CMASTERKEY_H
