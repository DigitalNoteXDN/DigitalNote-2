#ifndef BIP39_MNEMONIC_H
#define BIP39_MNEMONIC_H

#include <string>
#include <vector>

#include <bip39/checksum.h>
#include <bip39/entropy.h>
#include <bip39.h>

namespace BIP39
{
	class Seed;
	
	class Mnemonic
	{
		private:
			BIP39::LanguageCode _lang_code;
			BIP39::Words        _lang_words;
			BIP39::Entropy      _entropy;
			BIP39::CheckSum     _checksum;
			BIP39::Words        _mnemonic;
			
			bool _isLoaded() const;
			
			void _Generate(const BIP39::Entropy& entropy, const BIP39::CheckSum& checksum, BIP39::WordIndexs& word_indexs) const;
			void _Generate(const BIP39::WordIndexs& word_indexs, BIP39::Words& mnemonic) const;
			bool _Generate(const BIP39::Words& mnemonic, BIP39::WordIndexs& word_indexs) const;
			void _Generate(const BIP39::WordIndexs& word_indexs, BIP39::Entropy& entropy, BIP39::CheckSum& checksum) const;
			
		public:
			Mnemonic();
			~Mnemonic();
			
			const BIP39::Entropy& GetEntropy() const;
			const BIP39::CheckSum& GetCheckSum() const;
			const BIP39::Words& GetMnemonic() const;
			BIP39::Seed GetSeed() const;
			std::string GetStr() const;
			
			bool Set(const std::string& mnemonic_str);
			bool Set(const BIP39::Words& mnemonic);
			bool Set(const BIP39::Entropy& entropy, const BIP39::CheckSum& checksum);
			
			bool LoadLanguage(const BIP39::LanguageCode& lang_code = "EN");
			bool LoadExternLanguage(const BIP39::LanguageCode& lang_code = "EN");
			const BIP39::Words& GetLanguageWords() const;
			bool Find(const BIP39::Word& word, int* index) const;
			
			void Debug();
	};
}

#endif // BIP39_MNEMONIC_H
