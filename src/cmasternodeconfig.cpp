#include "compat.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include "net.h"
#include "util.h"
#include "base58.h"
#include "cmasternodeconfigentry.h"

#include "cmasternodeconfig.h"

CMasternodeConfig::CMasternodeConfig()
{
	entries = std::vector<CMasternodeConfigEntry>();
}

std::vector<CMasternodeConfigEntry>& CMasternodeConfig::getEntries()
{
	return entries;
}

void CMasternodeConfig::add(const std::string &alias, const std::string &ip, const std::string &privKey,
		const std::string &txHash, const std::string &outputIndex)
{
	CMasternodeConfigEntry cme(alias, ip, privKey, txHash, outputIndex);

	entries.push_back(cme);
}

bool CMasternodeConfig::read(boost::filesystem::path path)
{
	boost::filesystem::ifstream streamConfig(GetMasternodeConfigFile());

	if (!streamConfig.good())
	{
		return true; // No masternode.conf file is OK
	}

	for(std::string line; std::getline(streamConfig, line); )
	{
		if(line.empty())
		{
			continue;
		}
		
		std::istringstream iss(line);
		std::string alias, ip, privKey, txHash, outputIndex;
		
		iss.str(line);
		iss.clear();
		
		if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex))
		{
			LogPrintf("Could not parse masternode.conf line: %s\n", line.c_str());
			
			streamConfig.close();
			
			return false;
		}
		
		add(alias, ip, privKey, txHash, outputIndex);
	}

	streamConfig.close();

	return true;
}

