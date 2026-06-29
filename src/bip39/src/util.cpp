#include <string>

#include <bip39/entropy.h>
#include <bip39/seed.h>

template<typename T>
std::string HexStr(const T itbegin, const T itend, bool fSpaces)
{
	std::string rv;
	static const char hexmap[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
									 '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
	rv.reserve((itend-itbegin)*3);
	for(T it = itbegin; it < itend; ++it)
	{
		unsigned char val = (unsigned char)(*it);
		if(fSpaces && it != itbegin)
			rv.push_back(' ');
		rv.push_back(hexmap[val>>4]);
		rv.push_back(hexmap[val&15]);
	}

	return rv;
}

template<typename T>
std::string HexStr(const T& vch, bool fSpaces)
{
	return HexStr(vch.begin(), vch.end(), fSpaces);
}

template std::string HexStr<unsigned char*>(unsigned char*, unsigned char*, bool);
template std::string HexStr<unsigned char const*>(unsigned char const*, unsigned char const*, bool);

template std::string HexStr<BIP39::Entropy>(BIP39::Entropy const&, bool);
template std::string HexStr<BIP39::Seed>(BIP39::Seed const&, bool);
