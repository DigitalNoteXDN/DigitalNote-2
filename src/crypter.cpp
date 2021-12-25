#include "compat.h"

#include <openssl/aes.h>
#include <openssl/evp.h>

#include "util.h"
#include "ccrypter.h"

#include "crypter.h"

bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial &vchPlaintext, const uint256& nIV,
		std::vector<unsigned char> &vchCiphertext)
{
	CCrypter cKeyCrypter;
	std::vector<unsigned char> chIV(WALLET_CRYPTO_KEY_SIZE);

	memcpy(&chIV[0], &nIV, WALLET_CRYPTO_KEY_SIZE);

	if(!cKeyCrypter.SetKey(vMasterKey, chIV))
	{
		return false;
	}
	
	return cKeyCrypter.Encrypt(*((const CKeyingMaterial*)&vchPlaintext), vchCiphertext);
}

bool DecryptSecret(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCiphertext, const uint256& nIV,
		CKeyingMaterial& vchPlaintext)
{
	CCrypter cKeyCrypter;
	std::vector<unsigned char> chIV(WALLET_CRYPTO_KEY_SIZE);

	memcpy(&chIV[0], &nIV, WALLET_CRYPTO_KEY_SIZE);

	if(!cKeyCrypter.SetKey(vMasterKey, chIV))
	{
		return false;
	}

	return cKeyCrypter.Decrypt(vchCiphertext, *((CKeyingMaterial*)&vchPlaintext));
}

// General secure AES 256 CBC encryption routine
bool EncryptAES256(const SecureString& sKey, const SecureString& sPlaintext, const std::string& sIV, std::string& sCiphertext)
{
    // max ciphertext len for a n bytes of plaintext is
    // n + AES_BLOCK_SIZE - 1 bytes
    int nLen = sPlaintext.size();
    int nCLen = nLen + AES_BLOCK_SIZE;
    int nFLen = 0;

    // Verify key sizes
    if(sKey.size() != 32 || sIV.size() != AES_BLOCK_SIZE)
	{
        LogPrintf("crypter EncryptAES256 - Invalid key or block size: Key: %d sIV:%d\n", sKey.size(), sIV.size());
        
		return false;
    }

    // Prepare output buffer
    sCiphertext.resize(nCLen);

    // Perform the encryption
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    bool fOk = true;

    EVP_CIPHER_CTX_init(ctx);
    
	if (fOk)
	{
		fOk = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, (const unsigned char*) &sKey[0], (const unsigned char*) &sIV[0]);
	}
	
    if (fOk)
	{
		fOk = EVP_EncryptUpdate(ctx, (unsigned char*) &sCiphertext[0], &nCLen, (const unsigned char*) &sPlaintext[0], nLen);
	}
	
    if (fOk)
	{
		fOk = EVP_EncryptFinal_ex(ctx, (unsigned char*) (&sCiphertext[0])+nCLen, &nFLen);
	}
	
    EVP_CIPHER_CTX_cleanup(ctx);
    EVP_CIPHER_CTX_free(ctx);

    if (!fOk)
	{
		return false;
	}
	
    sCiphertext.resize(nCLen + nFLen);
    
	return true;
}

bool DecryptAES256(const SecureString& sKey, const std::string& sCiphertext, const std::string& sIV, SecureString& sPlaintext)
{
    // plaintext will always be equal to or lesser than length of ciphertext
    int nLen = sCiphertext.size();
    int nPLen = nLen, nFLen = 0;

    // Verify key sizes
    if(sKey.size() != 32 || sIV.size() != AES_BLOCK_SIZE)
	{
        LogPrintf("crypter DecryptAES256 - Invalid key or block size\n");
        
		return false;
    }

    sPlaintext.resize(nPLen);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    bool fOk = true;

    EVP_CIPHER_CTX_init(ctx);
    
	if (fOk)
	{
		fOk = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, (const unsigned char*) &sKey[0], (const unsigned char*) &sIV[0]);
	}
	
    if (fOk)
	{
		fOk = EVP_DecryptUpdate(ctx, (unsigned char *) &sPlaintext[0], &nPLen, (const unsigned char *) &sCiphertext[0], nLen);
	}
	
    if (fOk)
	{
		fOk = EVP_DecryptFinal_ex(ctx, (unsigned char *) (&sPlaintext[0])+nPLen, &nFLen);
	}
	
    EVP_CIPHER_CTX_cleanup(ctx);
    EVP_CIPHER_CTX_free(ctx);

    if (!fOk)
	{
		return false;
	}
	
    sPlaintext.resize(nPLen + nFLen);
	
    return true;
}

