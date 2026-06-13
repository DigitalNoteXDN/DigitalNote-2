#include <string.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <bip39/entropy.h>
#include <bip39/checksum.h>

#include "../util.h"

BIP39::Entropy::Entropy()
{
	memset(&this->_vch[0], 0, 32);
}

BIP39::Entropy::~Entropy()
{
	memset(&this->_vch[0], 0, 32);
}

BIP39::Entropy::Entropy(const BIP39::Data &data)
{
	memcpy(this->_vch, const_cast<unsigned char*>(&data.begin()[0]), 32);
}

std::string BIP39::Entropy::GetStr() const
{
	return "0x" + HexStr(*this);
}

bool BIP39::Entropy::Set(const BIP39::Data &data)
{
	if(data.size() != 32)
	{
		return false;
	}
	
	memcpy(this->_vch, const_cast<unsigned char*>(&data.begin()[0]), 32);
	
	return true;
}

unsigned int BIP39::Entropy::size() const
{
	return 32;
}

const unsigned char* BIP39::Entropy::begin() const
{
	return this->_vch;
}

const unsigned char* BIP39::Entropy::end() const
{
	return this->_vch + 32;
}

const unsigned char& BIP39::Entropy::operator[](unsigned int pos) const
{
	return this->_vch[pos];
}

bool BIP39::Entropy::genRandom()
{
	if(RAND_bytes(this->_vch, 32) == 0)
	{
		return false;
	}
	
	return true;
}

bool BIP39::Entropy::genCheckSum(BIP39::CheckSum& checksum) const
{
	unsigned char hash[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	
	// Clear checksum
	checksum.Set(0x0);
	
	// Create context and Initialize sha256 in context
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	if(ctx == NULL || EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 0)
	{
		return false;
	}
	
	// Initialize EVP space with data and make the hash
	if(
		EVP_DigestUpdate(ctx, this->begin(), 32) == 0 ||
		EVP_DigestFinal_ex(ctx, hash, &size) == 0
	)
	{
		// Free EVP
		EVP_MD_CTX_free(ctx);
		
		return false;
	}
	
	// Set checksum
	checksum.Set(static_cast<uint8_t>(hash[0]));
	
	// Free EVP
	EVP_MD_CTX_free(ctx);
	
	return true;
}

BIP39::Data BIP39::Entropy::Raw() const
{
	BIP39::Data data;

	data.insert(data.end(), this->_vch, this->_vch + 32);

	return data;
}

