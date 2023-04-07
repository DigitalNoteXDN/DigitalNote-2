#include "cmasternodeconfigentry.h"

CMasternodeConfigEntry::CMasternodeConfigEntry(const std::string &alias, const std::string &ip, const std::string &privKey, const std::string &txHash,
		const std::string &outputIndex)
{
	this->alias = alias;
	this->ip = ip;
	this->privKey = privKey;
	this->txHash = txHash;
	this->outputIndex = outputIndex;
}

const std::string& CMasternodeConfigEntry::getAlias() const
{
	return alias;
}

void CMasternodeConfigEntry::setAlias(const std::string& alias)
{
	this->alias = alias;
}

const std::string& CMasternodeConfigEntry::getOutputIndex() const
{
	return outputIndex;
}

void CMasternodeConfigEntry::setOutputIndex(const std::string& outputIndex)
{
	this->outputIndex = outputIndex;
}

const std::string& CMasternodeConfigEntry::getPrivKey() const
{
	return privKey;
}

void CMasternodeConfigEntry::setPrivKey(const std::string& privKey)
{
	this->privKey = privKey;
}

const std::string& CMasternodeConfigEntry::getTxHash() const
{
	return txHash;
}

void CMasternodeConfigEntry::setTxHash(const std::string& txHash)
{
	this->txHash = txHash;
}

const std::string& CMasternodeConfigEntry::getIp() const
{
	return ip;
}

void CMasternodeConfigEntry::setIp(const std::string& ip)
{
	this->ip = ip;
}

