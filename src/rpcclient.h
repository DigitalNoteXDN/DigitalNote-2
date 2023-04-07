#ifndef RPCCLIENT_H
#define RPCCLIENT_H

#include <string>
#include <vector>

#include "json/json_spirit_value.h"

int CommandLineRPC(int argc, char *argv[]);

json_spirit::Array RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams);

#endif // RPCCLIENT_H
