/*
	Reference:
		https://en.bitcoin.it/wiki/BIP_0039
		https://learnmeabitcoin.com/technical/mnemonic
		https://github.com/bitcoin/bips/tree/master/bip-0039
		https://pypi.org/project/bip-utils/2.3.0/#files
*/

#include <iostream>
#include <fstream>
#include <bip39/entropy.h>
#include <bip39/checksum.h>
#include <bip39/mnemonic.h>
#include <bip39/seed.h>

#include <openssl/opensslv.h>

#include "util.h"

void test_openssl()
{
	std::cout << "--- OPENSSL ---" << std::endl;
	std::cout << OPENSSL_VERSION_TEXT  << std::endl;
}

void test_new_entropy()
{
	BIP39::Entropy entropy;
	
	std::cout << "--- NEW ENTROPY ---" << std::endl;
	
	// Generate random entropy
	if(entropy.genRandom())
	{
		std::cout << "Entropy = " << entropy.GetStr() << std::endl;
	}
	else
	{
		std::cout << "Generate Entropy failed :(" << std::endl;
	}
}

void test_new_checksum()
{
	BIP39::Entropy entropy;
	BIP39::CheckSum checksum;
	
	std::cout << "--- NEW CHECKSUM ---" << std::endl;
	
	// Generate random entropy
	if(!entropy.genRandom())
	{
		std::cout << "Failed to generate new random entropy." << std::endl;
		return;
	}
	
	// Generate checksum
	if(!entropy.genCheckSum(checksum))
	{
		std::cout << "Failed to generate checksum." << std::endl;
		return;
	}
	
	// Test newly generated entropy and checksum
	std::cout << "Entropy  = " << entropy.GetStr() << std::endl;
	std::cout << "Checksum = " << checksum.GetStr() << std::endl;
	std::cout << "checksum.isValid() = " << std::boolalpha << checksum.isValid(entropy) << std::endl;
	
	// Test modified checksum
	checksum.Set(0x0);
	std::cout << "checksum.isValid() = " << std::boolalpha << checksum.isValid(entropy) << std::endl;
}

void test_new_mnemonic(const BIP39::LanguageCode& lang_code = "EN")
{
	int index;
	BIP39::Mnemonic mnemonic, mnemonic2;
	BIP39::Entropy entropy;
	BIP39::CheckSum checksum;
	
	std::cout << "--- NEW MNEMONIC ---" << std::endl;
	
	// Load the english words database
	if(!mnemonic.LoadLanguage(lang_code) || !mnemonic2.LoadLanguage(lang_code))
	{
		std::cout << "Failed to load language." << std::endl;
		return;
	}
	
	std::cout << "Found " << mnemonic.GetLanguageWords().size() << " total words." << std::endl;
	
	// Check if searching through database works
	if(mnemonic.Find("acid", &index))
	{
		std::cout << "Found 'acid' on position " << index << std::endl;
	}
	
	// Generate random entropy
	if(!entropy.genRandom())
	{
		std::cout << "Failed to generate new random entropy." << std::endl;
		return;
	}
	
	// Generate checksum
	if(!entropy.genCheckSum(checksum))
	{
		std::cout << "Failed to generate checksum." << std::endl;
		return;
	}
	
	// Generate mnemonic with entropy and checksum
	if(!mnemonic.Set(entropy, checksum))
	{
		std::cout << "Failed to generate mnemonic with entropy and checksum." << std::endl;
		return;
	}
	
	// Generate mnemonic with mnemonic string
	if(!mnemonic2.Set(mnemonic.GetStr()))
	{
		std::cout << "Failed to generate mnemonic with mnemonic string." << std::endl;
		return;
	}
	
	//mnemonic.Debug();
	mnemonic2.Debug();
}

void _gen_lang_words(std::ofstream& ofs, const BIP39::LanguageCode& lang_code, const std::string variable_name)
{
	BIP39::Mnemonic mnemonic;
	BIP39::Words database;
	
	mnemonic.LoadExternLanguage(lang_code);
	database = mnemonic.GetLanguageWords();
	
	ofs << "BIP39::Words " << variable_name << " = {";
	
	for(int i = 0; i < database.size(); i++)
	{
		if(i % 8 == 0)
		{
			ofs << std::endl << "	";
		}
		
		ofs << "\"" << database[i] << "\"";
		
		if(i != database.size() - 1)
		{
			ofs << ", ";
		}
	}
	ofs << std::endl;
	ofs << "};" << std::endl;
}

/*
	Generate database.cpp with vector and map with all the languages and words.
	This to avoid to use extern .txt files.
*/
void gen_database_cpp()
{
	std::ofstream ofs;
	
	// Open output file
	ofs.open("database.cpp", std::ifstream::out | std::ofstream::trunc);
	if(!ofs.is_open())
	{
		return;
	}
	
	ofs << "#include <map>" << std::endl
		<< "#include <bip39.h>" << std::endl
		<< std::endl;
	
	_gen_lang_words(ofs, "zh-CN",  "database_zhCN");
	_gen_lang_words(ofs, "zh-CHT", "database_zhCHT");
	_gen_lang_words(ofs, "CZ",    "database_CZ");
	_gen_lang_words(ofs, "EN",    "database_EN");
	_gen_lang_words(ofs, "FR",    "database_FR");
	_gen_lang_words(ofs, "IT",    "database_IT");
	_gen_lang_words(ofs, "JP",    "database_JP");
	_gen_lang_words(ofs, "PT",    "database_PT");
	_gen_lang_words(ofs, "ES",    "database_ES");
	
	ofs << std::endl
		<< "std::map<const BIP39::LanguageCode, const BIP39::Words> lang_code_database = {" << std::endl
		<< "	{ \"zh-CN\",  database_zhCN  }," << std::endl
		<< "	{ \"zh-CHT\", database_zhCHT }," << std::endl
		<< "	{ \"CZ\",     database_CZ    }," << std::endl
		<< "	{ \"EN\",     database_EN    }," << std::endl
		<< "	{ \"FR\",     database_FR    }," << std::endl
		<< "	{ \"IT\",     database_IT    }," << std::endl
		<< "	{ \"JP\",     database_JP    }," << std::endl
		<< "	{ \"PT\",     database_PT    }," << std::endl
		<< "	{ \"ES\",     database_ES    }," << std::endl
		<< "};";
	
	// Close output file
	ofs.close();
}

int main()
{
	test_openssl();
	test_new_entropy();
	test_new_checksum();
	test_new_mnemonic("EN");
	
	//gen_database_cpp();
	
	return 0;
}

