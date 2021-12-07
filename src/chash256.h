#ifndef CHASH256_H
#define CHASH256_H

#include "crypto/common/sha256.h"

/** A hasher class for DigitalNote's 256-bit hash (double SHA-256). */
class CHash256
{
private:
    CSHA256 sha;
	
public:
    static const size_t OUTPUT_SIZE = CSHA256::OUTPUT_SIZE;

    void Finalize(unsigned char hash[OUTPUT_SIZE]);
    CHash256& Write(const unsigned char *data, size_t len);
    CHash256& Reset();
};

#endif // CHASH256_H
