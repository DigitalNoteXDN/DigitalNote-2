#include <bip39/checksum.h>
#include <bip39/entropy.h>

#include "../util.h"

BIP39::CheckSum::CheckSum() : _sum(0)
{
	
}

BIP39::CheckSum::~CheckSum()
{
	this->_sum = 0x0;
}

BIP39::CheckSum::CheckSum(const uint8_t sum)
{
	this->_sum = sum;
}

void BIP39::CheckSum::Set(const uint8_t sum)
{
	this->_sum = sum;
}

uint8_t BIP39::CheckSum::Get() const
{
	return this->_sum;
}

std::string BIP39::CheckSum::GetStr() const
{	
	return "0x" + HexStr(&this->_sum, &this->_sum + 1);
}

bool BIP39::CheckSum::isValid(const BIP39::Entropy& entropy) const
{
	BIP39::CheckSum checksum;
	
	// Generate checksum
	if(!entropy.genCheckSum(checksum))
	{
		return false;
	}
	
	return this->_sum == checksum.Get();
}

