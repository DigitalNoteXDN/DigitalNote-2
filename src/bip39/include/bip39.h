#ifndef BIP39_H
#define BIP39_H

#include <string>
#include <vector>

namespace BIP39
{
	typedef std::vector<unsigned char>    Data;
	typedef std::string                   Word;
	typedef std::vector<BIP39::Word>      Words;
	typedef unsigned int                  WordIndex;
	typedef std::vector<BIP39::WordIndex> WordIndexs;
	typedef std::string                   LanguageCode;
}

#endif // BIP39_H
