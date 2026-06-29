#include <string.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>
#include <bip39/seed.h>

#include "../util.h"

BIP39::Seed::Seed()
{
	memset(&this->_vch[0], 0, 32);
}

BIP39::Seed::~Seed()
{
	memset(&this->_vch[0], 0, 32);
}

std::string BIP39::Seed::GetStr() const
{
	return "0x" + HexStr(*this);
}

bool BIP39::Seed::Set(const std::string& password)
{
	unsigned char salt[9] = "mnemonic";
	
	return (PKCS5_PBKDF2_HMAC(password.c_str(), password.size(), salt, 8, 2048, EVP_sha256(), 32, _vch) == 0);
}

std::string BIP39::Seed::GetPrivKey() const
{
	//CKey vchSecret;
	
	//vchSecret.Set(&this->_vch[0], &this->_vch[0] + this->size(), false);
	
	//CPrivKey privkey = vchSecret.GetPrivKey();
	//std::cout << "Private key = " << HexStr<CPrivKey::iterator>(privkey.begin(), privkey.end()) << std::endl;
	
	//return CDigitalNoteSecret(vchSecret).ToString();
	
	return "";
}

unsigned int BIP39::Seed::size() const
{
	return 32;
}

const unsigned char* BIP39::Seed::begin() const
{
	return this->_vch;
}

const unsigned char* BIP39::Seed::end() const
{
	return this->_vch + 32;
}

const unsigned char& BIP39::Seed::operator[](unsigned int pos) const
{
	return this->_vch[pos];
}

