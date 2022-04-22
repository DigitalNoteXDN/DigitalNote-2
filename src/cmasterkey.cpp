#include <cassert>

#include "serialize.h"
#include "cdatastream.h"

#include "cmasterkey.h"

CMasterKey::CMasterKey()
{
	// 25000 rounds is just under 0.1 seconds on a 1.86 GHz Pentium M
	// ie slightly lower than the lowest hardware we need bother supporting
	nDeriveIterations = 25000;
	nDerivationMethod = 1;
	vchOtherDerivationParameters = std::vector<unsigned char>(0);
}

CMasterKey::CMasterKey(unsigned int nDerivationMethodIndex)
{
	switch (nDerivationMethodIndex)
	{
		case 0: // sha512
		default:
			nDeriveIterations = 25000;
			nDerivationMethod = 0;
			vchOtherDerivationParameters = std::vector<unsigned char>(0);
		break;

		case 1: // scrypt+sha512
			nDeriveIterations = 10000;
			nDerivationMethod = 1;
			vchOtherDerivationParameters = std::vector<unsigned char>(0);
		break;
	}
}

unsigned int CMasterKey::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;

	s.nType = nType;
	s.nVersion = nVersion;
	
	READWRITE(vchCryptedKey);
	READWRITE(vchSalt);
	READWRITE(nDerivationMethod);
	READWRITE(nDeriveIterations);
	READWRITE(vchOtherDerivationParameters);
	
	return nSerSize;
}

template<typename Stream>
void CMasterKey::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(vchCryptedKey);
	READWRITE(vchSalt);
	READWRITE(nDerivationMethod);
	READWRITE(nDeriveIterations);
	READWRITE(vchOtherDerivationParameters);
}

template<typename Stream>
void CMasterKey::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(vchCryptedKey);
	READWRITE(vchSalt);
	READWRITE(nDerivationMethod);
	READWRITE(nDeriveIterations);
	READWRITE(vchOtherDerivationParameters);
}

template void CMasterKey::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CMasterKey::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

