#ifndef DESCRIBEADDRESSVISITER_H
#define DESCRIBEADDRESSVISITER_H

#include <boost/variant/static_visitor.hpp>
#include <json/json_spirit_value.h>

#include "enums/isminetype.h"

class CNoDestination;
class CKeyID;
class CScriptID;
class CStealthAddress;

class DescribeAddressVisitor : public boost::static_visitor<json_spirit::Object>
{
private:
	isminetype mine;

public:
	DescribeAddressVisitor(isminetype mineIn);
	
	json_spirit::Object operator()(const CNoDestination &dest) const;
	json_spirit::Object operator()(const CKeyID &keyID) const;
	json_spirit::Object operator()(const CScriptID &scriptID) const;
	json_spirit::Object operator()(const CStealthAddress &stxAddr) const;
};

#endif // DESCRIBEADDRESSVISITER_H
