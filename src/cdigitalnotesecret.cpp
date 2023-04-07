#include <cassert>

#include "ckey.h"
#include "cchainparams.h"
#include "chainparams.h"

#include "cdigitalnotesecret.h"

CDigitalNoteSecret::CDigitalNoteSecret()
{
	
}

CDigitalNoteSecret::CDigitalNoteSecret(const CKey& vchSecret)
{
	SetKey(vchSecret);
}

void CDigitalNoteSecret::SetKey(const CKey& vchSecret)
{
	assert(vchSecret.IsValid());

	SetData(Params().Base58Prefix(CChainParams_Base58Type::SECRET_KEY), vchSecret.begin(), vchSecret.size());

	if (vchSecret.IsCompressed())
	{
		vchData.push_back(1);
	}
}

CKey CDigitalNoteSecret::GetKey()
{
	CKey ret;

	ret.Set(&vchData[0], &vchData[32], vchData.size() > 32 && vchData[32] == 1);

	return ret;
}

bool CDigitalNoteSecret::IsValid() const
{
	bool fExpectedFormat = vchData.size() == 32 || (vchData.size() == 33 && vchData[32] == 1);
	bool fCorrectVersion = vchVersion == Params().Base58Prefix(CChainParams_Base58Type::SECRET_KEY);

	return fExpectedFormat && fCorrectVersion;
}

bool CDigitalNoteSecret::SetString(const char* pszSecret)
{
	return CBase58Data::SetString(pszSecret) && IsValid();
}

bool CDigitalNoteSecret::SetString(const std::string& strSecret)
{
	return SetString(strSecret.c_str());
}

