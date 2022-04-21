#ifndef JSONREQUEST_H
#define JSONREQUEST_H

#include "json/json_spirit_reader_template.h"

class JSONRequest
{
public:
	json_spirit::Value id;
	std::string strMethod;
	json_spirit::Array params;

	JSONRequest();
	void parse(const json_spirit::Value& valRequest);
};

#endif // JSONREQUEST_H
