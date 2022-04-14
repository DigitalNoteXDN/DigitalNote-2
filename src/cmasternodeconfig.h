#ifndef CMASTERNODECONFIG_H
#define CMASTERNODECONFIG_H

#include <string>
#include <vector>

class CMasternodeConfig;
class CMasternodeConfigEntry;

namespace boost
{
	namespace filesystem
	{
		class path;
	}
}

class CMasternodeConfig
{
private:
	std::vector<CMasternodeConfigEntry> entries;

public:
	CMasternodeConfig();

	std::vector<CMasternodeConfigEntry>& getEntries();

	bool read(boost::filesystem::path path);
	void add(const std::string &alias, const std::string &ip, const std::string &privKey, const std::string &txHash,
			const std::string &outputIndex);
};

#endif // CMASTERNODECONFIG_H
