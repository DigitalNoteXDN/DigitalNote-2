#ifndef MASTERNODECONFIGENTRY_H
#define MASTERNODECONFIGENTRY_H

#include <string>

class CMasternodeConfigEntry
{

private:
	std::string alias;
	std::string ip;
	std::string privKey;
	std::string txHash;
	std::string outputIndex;
	
public:

	CMasternodeConfigEntry(const std::string &alias, const std::string &ip, const std::string &privKey, const std::string &txHash,
			const std::string &outputIndex);
	
	const std::string& getAlias() const;
	void setAlias(const std::string& alias);
	const std::string& getOutputIndex() const;
	void setOutputIndex(const std::string& outputIndex);
	const std::string& getPrivKey() const;
	void setPrivKey(const std::string& privKey);
	const std::string& getTxHash() const;
	void setTxHash(const std::string& txHash);
	const std::string& getIp() const;
	void setIp(const std::string& ip);
};

#endif // MASTERNODECONFIGENTRY_H
