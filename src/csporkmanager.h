#ifndef CSPORKMANAGER_H
#define CSPORKMANAGER_H

#include <vector>
#include <string>

class CSporkMessage;

class CSporkManager
{
private:
	std::vector<unsigned char> vchSig;
	std::string strMasterPrivKey;
	std::string strTestPubKey;
	std::string strMainPubKey;

public:
	CSporkManager();

	std::string GetSporkNameByID(int id);
	int GetSporkIDByName(std::string strName);
	bool UpdateSpork(int nSporkID, int64_t nValue);
	bool SetPrivKey(const std::string &strPrivKey);
	bool CheckSignature(CSporkMessage& spork);
	bool Sign(CSporkMessage& spork);
	void Relay(CSporkMessage& msg);
};

#endif // CSPORKMANAGER_H
