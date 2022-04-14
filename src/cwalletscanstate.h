#ifndef CWALLETSCANSTATE_H
#define CWALLETSCANSTATE_H

#include <vector>

class uint256;

class CWalletScanState
{
public:
	unsigned int nKeys;
	unsigned int nCKeys;
	unsigned int nKeyMeta;
	bool fIsEncrypted;
	bool fAnyUnordered;
	int nFileVersion;
	std::vector<uint256> vWalletUpgrade;

	CWalletScanState()
	{
		nKeys = nCKeys = nKeyMeta = 0;
		fIsEncrypted = false;
		fAnyUnordered = false;
		nFileVersion = 0;
	}
};

#endif // CWALLETSCANSTATE_H
