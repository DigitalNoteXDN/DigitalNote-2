#ifndef CCRYPTER_H
#define CCRYPTER_H

#include <vector>

#include "crypter.h"
#include "allocators/securestring.h"

/** Encryption/decryption context with key information */
class CCrypter
{
private:
	unsigned char chKey[WALLET_CRYPTO_KEY_SIZE];
	unsigned char chIV[WALLET_CRYPTO_KEY_SIZE];
	bool fKeySet;

public:
	CCrypter();
	~CCrypter();

	bool SetKeyFromPassphrase(const SecureString &strKeyData, const std::vector<unsigned char>& chSalt,
			const unsigned int nRounds, const unsigned int nDerivationMethod);
	bool Encrypt(const CKeyingMaterial& vchPlaintext, std::vector<unsigned char> &vchCiphertext);
	bool Decrypt(const std::vector<unsigned char>& vchCiphertext, CKeyingMaterial& vchPlaintext);
	bool SetKey(const CKeyingMaterial& chNewKey, const std::vector<unsigned char>& chNewIV);
	void CleanKey();
};

#endif // CCRYPTER_H
