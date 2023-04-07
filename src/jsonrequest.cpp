#include "json/json_spirit_utils.h"

#include "enums/rpcerrorcode.h"
#include "rpcprotocol.h"
#include "util.h"

#include "jsonrequest.h"

JSONRequest::JSONRequest()
{
	id = json_spirit::Value::null;
}

void JSONRequest::parse(const json_spirit::Value& valRequest)
{
	// Parse request
	if (valRequest.type() != json_spirit::obj_type)
	{
		throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid Request object");
	}

	const json_spirit::Object& request = valRequest.get_obj();

	// Parse id now so errors from here on will have the id
	id = find_value(request, "id");

	// Parse method
	json_spirit::Value valMethod = find_value(request, "method");

	if (valMethod.type() == json_spirit::null_type)
	{
		throw JSONRPCError(RPC_INVALID_REQUEST, "Missing method");
	}

	if (valMethod.type() != json_spirit::str_type)
	{
		throw JSONRPCError(RPC_INVALID_REQUEST, "Method must be a string");
	}

	strMethod = valMethod.get_str();

	if (strMethod != "getwork" && strMethod != "getblocktemplate")
	{
		LogPrint("rpc", "ThreadRPCServer method=%s\n", strMethod);
	}

	// Parse params
	json_spirit::Value valParams = find_value(request, "params");

	if (valParams.type() == json_spirit::array_type)
	{
		params = valParams.get_array();
	}
	else if (valParams.type() == json_spirit::null_type)
	{
		params = json_spirit::Array();
	}
	else
	{
		throw JSONRPCError(RPC_INVALID_REQUEST, "Params must be an array");
	}
}

