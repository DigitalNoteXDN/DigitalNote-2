#include "util.h"
#include "json/json_spirit_value.h"

json_spirit::Value debugrpcallowip(const json_spirit::Array& params, bool fHelp)
{
	json_spirit::Object obj;
	const std::vector<std::string>& vRpcAllowIp = mapMultiArgs["-rpcallowip"];
	
	for(std::string srcRpcAllowIp : vRpcAllowIp)
	{
		obj.push_back(json_spirit::Pair("-rpcallowip=", srcRpcAllowIp));
	}
	
	return obj;
}