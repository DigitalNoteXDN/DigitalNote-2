#ifndef CDNSSEEDDATA_H
#define CDNSSEEDDATA_H

#include <string>

struct CDNSSeedData
{
	std::string name, host;

	CDNSSeedData(const std::string& strName, const std::string& strHost) : name(strName), host(strHost)
	{
		
	}
};

#endif // CDNSSEEDDATA_H
