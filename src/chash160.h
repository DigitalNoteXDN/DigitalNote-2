#ifndef CHASH160_H
#define CHASH160_H

#include "crypto/common/sha256.h"
#include "crypto/common/ripemd160.h"

/** A hasher class for DigitalNote's 160-bit hash (SHA-256 + RIPEMD-160). */
class CHash160
{
private:
    CSHA256 sha;
	
public:
    static const size_t OUTPUT_SIZE = CRIPEMD160::OUTPUT_SIZE;

    void Finalize(unsigned char hash[OUTPUT_SIZE]);
    CHash160& Write(const unsigned char *data, size_t len);
    CHash160& Reset();
};

#endif // CHASH160_H
