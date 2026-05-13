#ifndef CSPORKMANAGER_H
#define CSPORKMANAGER_H

#include <cstdint>
#include <vector>
#include <string>

class CSporkMessage;

class CSporkManager
{
private:
	std::vector<unsigned char> vchSig;
	std::string strMasterPrivKey;
	// Legacy pubkeys -- retained for verification of any sporks that
	// may have been signed with the original key.  Whoever held the
	// corresponding private key is unknown to active project members
	// and presumed unavailable.  Do not rely on these for operational
	// spork capability; use the v2.0.0.7 keys below.
	std::string strTestPubKey;
	std::string strMainPubKey;
	// v2.0.0.7 operative pubkeys -- newly generated, private key held
	// by current project owner.  CheckSignature accepts signatures
	// from either the legacy key OR these, so the legacy keys remain
	// honored for backward compat while the v2.0.0.7 keys are what
	// the project uses going forward.
	std::string strTestPubKeyNew;
	std::string strMainPubKeyNew;

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
