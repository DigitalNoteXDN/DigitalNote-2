#include <string>
#include <fstream>
#include <map>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <bip39.h>
#include <bip39/seed.h>
#include <bip39/mnemonic.h>

std::map<const BIP39::LanguageCode, const std::string> lang_code_filepath = {
	{ "zh-CN",  "../data/chinese_simplified.txt" },
	{ "zh-CHT", "../data/chinese_traditional.txt" },
	{ "CZ",     "../data/czech.txt" },
	{ "EN",     "../data/english.txt" },
	{ "FR",     "../data/french.txt" },
	{ "IT",     "../data/italian.txt" },
	{ "JP",     "../data/japanese.txt" },
	{ "PT",     "../data/portuguese.txt" },
	{ "ES",     "../data/spanish.txt" }
};

extern std::map<const BIP39::LanguageCode, const BIP39::Words> lang_code_database;

/*
	Checks if language database is loaded
*/
bool BIP39::Mnemonic::_isLoaded() const
{
	return this->_lang_words.size() == 2048;
}

/*
	Generate word indexs from entropy and checksum
	
	-> entropy
	-> checksum
	<- word_indexs
	
	Important: This function will not check the input and output content
*/
void BIP39::Mnemonic::_Generate(const BIP39::Entropy& entropy, const BIP39::CheckSum& checksum, BIP39::WordIndexs& word_indexs) const
{
	BIP39::Data data = entropy.Raw();
	
	// Collect word indexs
	word_indexs = {
		( static_cast<unsigned int>(data[ 0])         <<  3) + ((static_cast<unsigned int>(data[ 1]) & 0xE0) >> 5),
		((static_cast<unsigned int>(data[ 1]) & 0x1F) <<  6) + ((static_cast<unsigned int>(data[ 2]) & 0xFC) >> 2),
		((static_cast<unsigned int>(data[ 2]) & 0x03) <<  9) + ( static_cast<unsigned int>(data[ 3])         << 1) + ((static_cast<uint16_t>(data[4]) & 0x80) >> 7),
		((static_cast<unsigned int>(data[ 4]) & 0x7F) <<  4) + ((static_cast<unsigned int>(data[ 5]) & 0xF0) >> 4),
		((static_cast<unsigned int>(data[ 5]) & 0x0F) <<  7) + ((static_cast<unsigned int>(data[ 6]) & 0xFE) >> 1),
		((static_cast<unsigned int>(data[ 6]) & 0x01) << 10) + ((static_cast<unsigned int>(data[ 7])       ) << 2) + ((static_cast<uint16_t>(data[8]) & 0xC0) >> 6),
		((static_cast<unsigned int>(data[ 8]) & 0x3F) <<  5) + ((static_cast<unsigned int>(data[ 9]) & 0xF8) >> 3),
		((static_cast<unsigned int>(data[ 9]) & 0x07) <<  8) + ( static_cast<unsigned int>(data[10])             ),
		( static_cast<unsigned int>(data[11])         <<  3) + ((static_cast<unsigned int>(data[12]) & 0xE0) >> 5),
		((static_cast<unsigned int>(data[12]) & 0x1F) <<  6) + ((static_cast<unsigned int>(data[13]) & 0xFC) >> 2),
		((static_cast<unsigned int>(data[13]) & 0x03) <<  9) + ( static_cast<unsigned int>(data[14])         << 1) + ((static_cast<uint16_t>(data[15]) & 0x80) >> 7),
		((static_cast<unsigned int>(data[15]) & 0x7F) <<  4) + ((static_cast<unsigned int>(data[16]) & 0xF0) >> 4),
		((static_cast<unsigned int>(data[16]) & 0x0F) <<  7) + ((static_cast<unsigned int>(data[17]) & 0xFE) >> 1),
		((static_cast<unsigned int>(data[17]) & 0x01) << 10) + ((static_cast<unsigned int>(data[18])       ) << 2) + ((static_cast<uint16_t>(data[19]) & 0xC0) >> 6),
		((static_cast<unsigned int>(data[19]) & 0x3F) <<  5) + ((static_cast<unsigned int>(data[20]) & 0xF8) >> 3),
		((static_cast<unsigned int>(data[20]) & 0x07) <<  8) + ( static_cast<unsigned int>(data[21])             ),
		( static_cast<unsigned int>(data[22])         <<  3) + ((static_cast<unsigned int>(data[23]) & 0xE0) >> 5),
		((static_cast<unsigned int>(data[23]) & 0x1F) <<  6) + ((static_cast<unsigned int>(data[24]) & 0xFC) >> 2),
		((static_cast<unsigned int>(data[24]) & 0x03) <<  9) + ( static_cast<unsigned int>(data[25])         << 1) + ((static_cast<uint16_t>(data[26]) & 0x80) >> 7),
		((static_cast<unsigned int>(data[26]) & 0x7F) <<  4) + ((static_cast<unsigned int>(data[27]) & 0xF0) >> 4),
		((static_cast<unsigned int>(data[27]) & 0x0F) <<  7) + ((static_cast<unsigned int>(data[28]) & 0xFE) >> 1),
		((static_cast<unsigned int>(data[28]) & 0x01) << 10) + ((static_cast<unsigned int>(data[29])       ) << 2) + ((static_cast<uint16_t>(data[30]) & 0xC0) >> 6),
		((static_cast<unsigned int>(data[30]) & 0x3F) <<  5) + ((static_cast<unsigned int>(data[31]) & 0xF8) >> 3),
		((static_cast<unsigned int>(data[31]) & 0x07) <<  8) +   static_cast<unsigned int>(checksum.Get())
	};
}

/*
	Generate mnemonic from word indexs
	
	-> word_indexs
	<- mnemonic
	
	Important: This function will not check the input, output content or database
*/
void BIP39::Mnemonic::_Generate(const BIP39::WordIndexs& word_indexs, BIP39::Words& mnemonic) const
{
	// Clear mnemonic
	mnemonic.clear();
	
	for(BIP39::WordIndex index : word_indexs)
	{
		mnemonic.push_back(this->_lang_words[index]);
	}
}

/*
	Generate word indexs from mnemonic
	
	-> mnemonic
	<- word_indexs
	
	Important: This function will not check the input, output content or database
*/
bool BIP39::Mnemonic::_Generate(const BIP39::Words& mnemonic, BIP39::WordIndexs& word_indexs) const
{
	BIP39::Words::const_iterator found, begin, end;
		
	begin = this->_lang_words.begin();
	end = this->_lang_words.end();
	
	// Clear word indexs
	word_indexs.clear();
	
	for(const BIP39::Word word : mnemonic)
	{
		found = std::find(begin, end, word.c_str());
		
		// Find word index in database
		if(found == end)
		{
			word_indexs.clear();
			
			return false;
		}
		
		word_indexs.push_back(std::distance(begin, found));
	}
	
	return true;
}

/*
	Generate entropy and checksum from word indexs
	
	-> word indexs
	<- entropy
	<- checksum
	
	Important: This function will not check the input and output content
*/
void BIP39::Mnemonic::_Generate(const BIP39::WordIndexs& word_indexs, BIP39::Entropy& entropy, BIP39::CheckSum& checksum) const
{
	BIP39::Data data = {
		static_cast<unsigned char>(( word_indexs[ 0] & 0x7F8) >> 3                                     ),
		static_cast<unsigned char>(((word_indexs[ 0] & 0x007) << 5) + ((word_indexs[ 1] & 0x7C0) >>  6)),
		static_cast<unsigned char>(((word_indexs[ 1] & 0x03F) << 2) + ((word_indexs[ 2] & 0x600) >>  9)),
		static_cast<unsigned char>(( word_indexs[ 2] & 0x1FE) >> 1                                     ),
		static_cast<unsigned char>(((word_indexs[ 2] & 0x001) << 7) + ((word_indexs[ 3] & 0x7F0) >>  4)),
		static_cast<unsigned char>(((word_indexs[ 3] & 0x00F) << 4) + ((word_indexs[ 4] & 0x780) >>  7)),
		static_cast<unsigned char>(((word_indexs[ 4] & 0x07F) << 1) + ((word_indexs[ 5] & 0x400) >> 10)),
		static_cast<unsigned char>(( word_indexs[ 5] & 0x3FC) >> 2                                     ),
		static_cast<unsigned char>(((word_indexs[ 5] & 0x003) << 6) + ((word_indexs[ 6] & 0x7E0) >>  5)),
		static_cast<unsigned char>(((word_indexs[ 6] & 0x01F) << 3) + ((word_indexs[ 7] & 0x700) >>  8)),
		static_cast<unsigned char>(( word_indexs[ 7] & 0x0FF)	                                       ),
		static_cast<unsigned char>(( word_indexs[ 8] & 0x7F8) >> 3                                     ),
		static_cast<unsigned char>(((word_indexs[ 8] & 0x007) << 5) + ((word_indexs[ 9] & 0x7C0) >>  6)),
		static_cast<unsigned char>(((word_indexs[ 9] & 0x03F) << 2) + ((word_indexs[10] & 0x600) >>  9)),
		static_cast<unsigned char>(( word_indexs[10] & 0x1FE) >> 1                                     ),
		static_cast<unsigned char>(((word_indexs[10] & 0x001) << 7) + ((word_indexs[11] & 0x7F0) >>  4)),
		static_cast<unsigned char>(((word_indexs[11] & 0x00F) << 4) + ((word_indexs[12] & 0x780) >>  7)),
		static_cast<unsigned char>(((word_indexs[12] & 0x07F) << 1) + ((word_indexs[13] & 0x400) >> 10)),
		static_cast<unsigned char>(( word_indexs[13] & 0x3FC) >> 2                                     ),
		static_cast<unsigned char>(((word_indexs[13] & 0x003) << 6) + ((word_indexs[14] & 0x7E0) >>  5)),
		static_cast<unsigned char>(((word_indexs[14] & 0x01F) << 3) + ((word_indexs[15] & 0x700) >>  8)),
		static_cast<unsigned char>(( word_indexs[15] & 0x0FF)	                                       ),
		static_cast<unsigned char>(( word_indexs[16] & 0x7F8) >> 3                                     ),
		static_cast<unsigned char>(((word_indexs[16] & 0x007) << 5) + ((word_indexs[17] & 0x7C0) >>  6)),
		static_cast<unsigned char>(((word_indexs[17] & 0x03F) << 2) + ((word_indexs[18] & 0x600) >>  9)),
		static_cast<unsigned char>(( word_indexs[18] & 0x1FE) >> 1                                     ),
		static_cast<unsigned char>(((word_indexs[18] & 0x001) << 7) + ((word_indexs[19] & 0x7F0) >>  4)),
		static_cast<unsigned char>(((word_indexs[19] & 0x00F) << 4) + ((word_indexs[20] & 0x780) >>  7)),
		static_cast<unsigned char>(((word_indexs[20] & 0x07F) << 1) + ((word_indexs[21] & 0x400) >> 10)),
		static_cast<unsigned char>(( word_indexs[21] & 0x3FC) >> 2                                     ),
		static_cast<unsigned char>(((word_indexs[21] & 0x003) << 6) + ((word_indexs[22] & 0x7E0) >>  5)),
		static_cast<unsigned char>(((word_indexs[22] & 0x01F) << 3) + ((word_indexs[23] & 0x700) >>  8))
	};
	
	entropy.Set(data);
	checksum.Set(static_cast<uint8_t>(word_indexs[23] & 0xFF));
}

BIP39::Mnemonic::Mnemonic() : _lang_code("")
{
	
}

BIP39::Mnemonic::~Mnemonic()
{
	this->_lang_words.clear();
	this->_mnemonic.clear();
}

const BIP39::Entropy& BIP39::Mnemonic::GetEntropy() const
{
	return this->_entropy;
}

const BIP39::CheckSum& BIP39::Mnemonic::GetCheckSum() const
{
	return this->_checksum;
}

const BIP39::Words& BIP39::Mnemonic::GetMnemonic() const
{
	return this->_mnemonic;
}

BIP39::Seed BIP39::Mnemonic::GetSeed() const
{
	BIP39::Seed seed;
	
	seed.Set(this->GetStr());
	
	return seed;
}

std::string BIP39::Mnemonic::GetStr() const
{
	std::string v;
	BIP39::Words::const_iterator it, begin, end;
	
	begin = this->_mnemonic.begin();
	end = this->_mnemonic.end();
	
	for(it = begin; it != end; it++)
	{
		if(it != begin)
		{
			v += " ";
		}
		
		v += *it;
	}
	
	return v;
}

/*
	Set string words as mnemonic
*/
bool BIP39::Mnemonic::Set(const std::string& mnemonic_str)
{
	BIP39::Word word;
	BIP39::Words mnemonic;
	std::stringstream ss;
	
	// Convert string to stringstream
	ss << mnemonic_str;
	
	// Split string into separeted words
	while(std::getline(ss, word, ' '))
	{
		mnemonic.push_back(word);
	}
	
	// Check we got enough words
	if(mnemonic.size() != 24)
	{
		return false;
	}
	
	// Set words
	return this->Set(mnemonic);
}

/*
	Set vector words as mnemonic
*/
bool BIP39::Mnemonic::Set(const BIP39::Words& mnemonic)
{
	BIP39::WordIndexs word_indexs;
	BIP39::Entropy entropy;
	BIP39::CheckSum checksum;
	
	// Check database is loaded
	if(!this->_isLoaded())
	{
		return false;
	}
	
	// Generate word indexs
	if(!this->_Generate(mnemonic, word_indexs))
	{
		return false;
	}
	
	// Generate entropy and checksum
	this->_Generate(word_indexs, entropy, checksum);
	
	if(!checksum.isValid(entropy))
	{
		return false;
	}
	
	this->_entropy = entropy;
	this->_checksum = checksum;
	this->_mnemonic = mnemonic;
	
	return true;
}

/*
	Set entropy and checksum as mnemonic
*/
bool BIP39::Mnemonic::Set(const BIP39::Entropy& entropy, const BIP39::CheckSum& checksum)
{
	BIP39::WordIndexs word_indexs;
	
	// Check database is loaded and if checksum and entropy match
	if(!this->_isLoaded() || !checksum.isValid(entropy))
	{
		return false;
	}
	
	// Generate word indexs
	this->_Generate(entropy, checksum, word_indexs);
	
	// Generate mnemonic
	this->_Generate(word_indexs, this->_mnemonic);
	
	// Copy entropy and checksum
	this->_entropy = entropy;
	this->_checksum = checksum;
	
	return true;
}

bool BIP39::Mnemonic::LoadLanguage(const BIP39::LanguageCode& lang_code)
{
	if(lang_code_database.find(lang_code) == lang_code_database.end())
	{
		return false;
	}

	// Clear words
	this->_lang_code = lang_code;
	this->_lang_words.clear();
	this->_lang_words = lang_code_database[lang_code];
	
	return true;
}

bool BIP39::Mnemonic::LoadExternLanguage(const BIP39::LanguageCode& lang_code)
{
	std::string filepath;
	std::ifstream ifs;
	
	// Get language file path
	filepath = lang_code_filepath[lang_code];
	
	// Open input file
	ifs.open(filepath, std::ifstream::in);
	if(!ifs.is_open())
	{
		return false;
	}
	
	// Clear words
	this->_lang_code = lang_code;
	this->_lang_words.clear();
	
	// Iterate through the words inside the input file
	for (BIP39::Word word; std::getline(ifs, word); ) 
	{		
		this->_lang_words.push_back(word);
	}
	
	// Close input file
	ifs.close();
	
	return true;
}

/*
	Return the language database
*/
const BIP39::Words& BIP39::Mnemonic::GetLanguageWords() const
{
	return this->_lang_words;
}

/*
	Find word in database
	
	-> word
	<- index
*/
bool BIP39::Mnemonic::Find(const BIP39::Word& word, int* index) const
{
	BIP39::Words::const_iterator found, begin, end;
	
	if(!this->_isLoaded())
	{
		*index = -1;
		return false;
	}
	
	begin = this->_lang_words.begin();
	end = this->_lang_words.end();
	
	found = std::find(begin, end, word.c_str());
		
	if(found != end)
	{
		*index = std::distance(begin, found);
		
		return true;
	}
	
	*index = -1;
	return false;
}

void BIP39::Mnemonic::Debug()
{
	std::cout << "--- MNEMONIC CLASS ---" << std::endl;
	std::cout << "Language Code = " << this->_lang_code << std::endl;
	std::cout << "Language size = " << this->_lang_words.size() << std::endl;
	
	std::cout << "isLoaded = " << std::boolalpha << this->_isLoaded() << std::endl;
	
	std::cout << "Entropy  = " << this->_entropy.GetStr() << std::endl;
	std::cout << "Checksum = " << this->_checksum.GetStr() << std::endl;
	std::cout << "mnemonic = " << this->GetStr() << std::endl;
	std::cout << "seed     = " << this->GetSeed().GetStr() << std::endl;
}

