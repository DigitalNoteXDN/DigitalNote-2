#ifndef MAPNEWBLOCK_T_H
#define MAPNEWBLOCK_T_H

#include <map>
#include <utility>

class CScript;
class CBlock;
class uint256;

typedef std::map<uint256, std::pair<CBlock*, CScript> > mapNewBlock_t;

#endif // MAPNEWBLOCK_T_H
