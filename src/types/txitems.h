#ifndef TXITEMS_H
#define TXITEMS_H

#include <cstdint>
#include <map>

#include "types/txpair.h"

typedef std::multimap<int64_t, TxPair> TxItems;

#endif // TXITEMS_H
