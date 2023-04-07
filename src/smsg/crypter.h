#ifndef SMSG_CRYPTER_H
#define SMSG_CRYPTER_H

#include <cstdint>
#include <vector>

namespace DigitalNote {
namespace SMSG {

class Crypter
{
private:
	uint8_t chKey[32];
	uint8_t chIV[16];
	bool fKeySet;

public:
	Crypter();
	~Crypter();

	bool SetKey(const std::vector<uint8_t>& vchNewKey, uint8_t* chNewIV);
	bool SetKey(const uint8_t* chNewKey, uint8_t* chNewIV);
	bool Encrypt(uint8_t* chPlaintext, uint32_t nPlain, std::vector<uint8_t> &vchCiphertext);
	bool Decrypt(uint8_t* chCiphertext, uint32_t nCipher, std::vector<uint8_t>& vchPlaintext);
};

} // namespace SMSG
} // namespace DigitalNote

#endif // SMSG_CRYPTER_H
