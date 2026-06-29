#ifndef BIP39_SEED_H
#define BIP39_SEED_H

#include <string>

namespace BIP39
{
	class Seed
	{
		private:
			unsigned char _vch[32]; // 256 bits
		
		public:
			Seed();
			~Seed();
			
			std::string GetStr() const;
			bool Set(const std::string& password);
			
			std::string GetPrivKey() const;
			
			unsigned int size() const;
			const unsigned char *begin() const;
			const unsigned char *end() const;
			const unsigned char &operator[](unsigned int pos) const;
	};
}

#endif // BIP39_SEED_H
