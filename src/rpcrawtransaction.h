#ifndef RPCRAWTRANSACTION_H
#define RPCRAWTRANSACTION_H

#include "json/json_spirit_value.h"

class CTransaction;
class uint256;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry);

#endif // RPCRAWTRANSACTION_H
