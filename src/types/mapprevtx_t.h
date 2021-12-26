#ifndef MAPPREVTX_T_H
#define MAPPREVTX_T_H

#include <map>

class uint256;
class CTxIndex;
class CTransaction;

typedef std::map<uint256, std::pair<CTxIndex, CTransaction>> mapPrevTx_t;

#endif // MAPPREVTX_T_H