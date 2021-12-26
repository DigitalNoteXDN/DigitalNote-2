#ifndef CKEYINGMATERIAL_H
#define CKEYINGMATERIAL_H

#include <vector>

#include "allocators/secure_allocator.h"

typedef std::vector<unsigned char, secure_allocator<unsigned char>> CKeyingMaterial;

#endif // CKEYINGMATERIAL_H
