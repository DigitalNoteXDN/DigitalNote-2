#ifndef BIP39_ENTROPY_H
#define BIP39_ENTROPY_H

#include <string>
#include <vector>
#include <bip39.h>

namespace BIP39
{
	class CheckSum;
	
	class Entropy
	{
		private:
			unsigned char _vch[32]; // 256 bits
		
		public:
			Entropy();
			~Entropy();
			Entropy(const BIP39::Data &data);
			
			std::string GetStr() const;
			bool Set(const BIP39::Data &data);
			
			unsigned int size() const;
			const unsigned char *begin() const;
			const unsigned char *end() const;
			const unsigned char &operator[](unsigned int pos) const;
			
			bool genRandom();
			bool genCheckSum(BIP39::CheckSum& checksum) const;
			BIP39::Data Raw() const;
	};
}

#endif // BIP39_ENTROPY_H
