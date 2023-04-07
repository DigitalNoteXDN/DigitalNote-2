#ifndef RPCFN_TYPE_H
#define RPCFN_TYPE_H

#include "json/json_spirit_value.h"

typedef json_spirit::Value (*rpcfn_type)(const json_spirit::Array& params, bool fHelp);

#endif // RPCFN_TYPE_H
