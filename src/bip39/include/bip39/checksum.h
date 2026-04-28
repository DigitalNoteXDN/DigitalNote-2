#ifndef BIP39_CHECKSUM_H
#define BIP39_CHECKSUM_H

#include <cstdint>
#include <string>

namespace BIP39
{
	class Entropy;
	
	class CheckSum
	{
		private:
			uint8_t _sum; // 8 bits
		
		public:
			CheckSum();
			~CheckSum();
			CheckSum(const uint8_t sum);
			
			void Set(const uint8_t sum);
			uint8_t Get() const;
			std::string GetStr() const;
			
			bool isValid(const BIP39::Entropy& entropy) const;
	};
}

#endif // BIP39_CHECKSUM_H
