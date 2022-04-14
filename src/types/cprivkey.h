#ifndef CPRIVKEY_H
#define CPRIVKEY_H

#include <vector>

#include "allocators/secure_allocator.h"

// secure_allocator is defined in allocators.h
// CPrivKey is a serialized private key, with all parameters included (279 bytes)
typedef std::vector<unsigned char, secure_allocator<unsigned char> > CPrivKey;

#endif // CPRIVKEY_H
