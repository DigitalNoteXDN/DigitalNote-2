#ifndef CRPCTABLE_H
#define CRPCTABLE_H

#include <string>
#include <map>
#include <vector>

#include "json/json_spirit_value.h"

class CRPCCommand;

typedef std::map<std::string, const CRPCCommand*> mapCommands_t;

/**
 * DigitalNote RPC command dispatcher.
 */
class CRPCTable
{
private:
    mapCommands_t mapCommands;
	
public:
    CRPCTable();

    const CRPCCommand* operator[](std::string name) const;
    std::string help(const std::string &name) const;

    /**
     * Execute a method.
     * @param method   Method to execute
     * @param params   Array of arguments (JSON objects)
     * @returns Result of the call.
     * @throws an exception (json_spirit::Value) when an error happens.
     */
    json_spirit::Value execute(const std::string &method, const json_spirit::Array &params) const;
    std::vector<std::string> listCommands() const;
};

#endif // CRPCTABLE_H
