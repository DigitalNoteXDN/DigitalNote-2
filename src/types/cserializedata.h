#ifndef CSERIALIZEDATA_H
#define CSERIALIZEDATA_H

#include <vector>

#include "allocators/zero_after_free_allocator.h"

typedef std::vector<char, zero_after_free_allocator<char> > CSerializeData;

#endif // CSERIALIZEDATA_H