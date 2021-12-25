#ifndef RPCRAWTRANSACTION_H
#define RPCRAWTRANSACTION_H

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry);

#endif // RPCRAWTRANSACTION_H
