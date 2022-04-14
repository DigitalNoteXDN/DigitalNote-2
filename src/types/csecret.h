#ifndef CSECRET_H
#define CSECRET_H

#include <vector>

#include "allocators/secure_allocator.h"

// CSecret is a serialization of just the secret parameter (32 bytes)
typedef std::vector<unsigned char, secure_allocator<unsigned char> > CSecret;

#endif // CSECRET_H
