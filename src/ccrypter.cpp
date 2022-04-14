#include <openssl/evp.h>
#include <openssl/aes.h>

#include "uint/uint256.h"
#include "scrypt.h"
#include "allocators/lockedpagemanager.h"
#include "support/cleanse.h"

#include "ccrypter.h"

CCrypter::CCrypter()
{
	fKeySet = false;

	// Try to keep the key data out of swap (and be a bit over-careful to keep the IV that we don't even use out of swap)
	// Note that this does nothing about suspend-to-disk (which will put all our key data on disk)
	// Note as well that at no point in this program is any attempt made to prevent stealing of keys by reading the memory of the running process.
	LockedPageManager::Instance().LockRange(&chKey[0], sizeof chKey);
	LockedPageManager::Instance().LockRange(&chIV[0], sizeof chIV);
}

CCrypter::~CCrypter()
{
	CleanKey();

	LockedPageManager::Instance().UnlockRange(&chKey[0], sizeof chKey);
	LockedPageManager::Instance().UnlockRange(&chIV[0], sizeof chIV);
}

bool CCrypter::SetKeyFromPassphrase(const SecureString& strKeyData, const std::vector<unsigned char>& chSalt,
		const unsigned int nRounds, const unsigned int nDerivationMethod)
{
	if (nRounds < 1 || chSalt.size() != WALLET_CRYPTO_SALT_SIZE)
	{
		return false;
	}

	int i = 0;

	if (nDerivationMethod == 0)
	{
		i = EVP_BytesToKey(
			EVP_aes_256_cbc(),
			EVP_sha512(),
			&chSalt[0],
			(unsigned char *)&strKeyData[0],
			strKeyData.size(),
			nRounds,
			chKey,
			chIV
		);
	}

	if (nDerivationMethod == 1)
	{
		// Passphrase conversion
		uint256 scryptHash = scrypt_salted_multiround_hash(
			(const void*)strKeyData.c_str(),
			strKeyData.size(),
			&chSalt[0],
			8,
			nRounds
		);

		i = EVP_BytesToKey(
			EVP_aes_256_cbc(),
			EVP_sha512(),
			&chSalt[0],
			(unsigned char *)&scryptHash,
			sizeof scryptHash,
			nRounds,
			chKey,
			chIV
		);
		
		memory_cleanse(&scryptHash, sizeof scryptHash);
	}


	if (i != (int)WALLET_CRYPTO_KEY_SIZE)
	{
		memory_cleanse(chKey, sizeof(chKey));
		memory_cleanse(chIV, sizeof(chIV));
		
		return false;
	}

	fKeySet = true;

	return true;
}

bool CCrypter::Encrypt(const CKeyingMaterial& vchPlaintext, std::vector<unsigned char> &vchCiphertext)
{
	if (!fKeySet)
	{
		return false;
	}

	// max ciphertext len for a n bytes of plaintext is
	// n + AES_BLOCK_SIZE - 1 bytes
	int nLen = vchPlaintext.size();
	int nCLen = nLen + AES_BLOCK_SIZE, nFLen = 0;
	vchCiphertext = std::vector<unsigned char> (nCLen);

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

	bool fOk = true;

	EVP_CIPHER_CTX_init(ctx);

	if (fOk)
	{
		fOk = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, chKey, chIV);
	}

	if (fOk)
	{
		fOk = EVP_EncryptUpdate(ctx, &vchCiphertext[0], &nCLen, &vchPlaintext[0], nLen);
	}

	if (fOk)
	{
		fOk = EVP_EncryptFinal_ex(ctx, (&vchCiphertext[0])+nCLen, &nFLen);
	}

	EVP_CIPHER_CTX_cleanup(ctx);
	EVP_CIPHER_CTX_free(ctx);

	if (!fOk)
	{
		return false;
	}

	vchCiphertext.resize(nCLen + nFLen);

	return true;
}

bool CCrypter::Decrypt(const std::vector<unsigned char>& vchCiphertext, CKeyingMaterial& vchPlaintext)
{
	if (!fKeySet)
	{
		return false;
	}

	// plaintext will always be equal to or lesser than length of ciphertext
	int nLen = vchCiphertext.size();
	int nPLen = nLen, nFLen = 0;

	vchPlaintext = CKeyingMaterial(nPLen);

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

	bool fOk = true;

	EVP_CIPHER_CTX_init(ctx);

	if (fOk)
	{
		fOk = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, chKey, chIV);
	}

	if (fOk)
	{
		fOk = EVP_DecryptUpdate(ctx, &vchPlaintext[0], &nPLen, &vchCiphertext[0], nLen);
	}

	if (fOk)
	{
		fOk = EVP_DecryptFinal_ex(ctx, (&vchPlaintext[0])+nPLen, &nFLen);
	}

	EVP_CIPHER_CTX_cleanup(ctx);
	EVP_CIPHER_CTX_free(ctx);

	if (!fOk)
	{
		return false;
	}

	vchPlaintext.resize(nPLen + nFLen);

	return true;
}

bool CCrypter::SetKey(const CKeyingMaterial& chNewKey, const std::vector<unsigned char>& chNewIV)
{
	if (chNewKey.size() != WALLET_CRYPTO_KEY_SIZE || chNewIV.size() != WALLET_CRYPTO_KEY_SIZE)
	{
		return false;
	}

	memcpy(&chKey[0], &chNewKey[0], sizeof chKey);
	memcpy(&chIV[0], &chNewIV[0], sizeof chIV);

	fKeySet = true;

	return true;
}

void CCrypter::CleanKey()
{
	memory_cleanse(chKey, sizeof(chKey));
	memory_cleanse(chIV, sizeof(chIV));
	
	fKeySet = false;
}

