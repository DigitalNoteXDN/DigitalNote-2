#include "compat.h"

#include "cwallet.h"
#include "ckeypool.h"
#include "cscript.h"
#include "util.h"

#include "creservekey.h"

CReserveKey::CReserveKey(CWallet* pwalletIn)
{
	nIndex = -1;
	pwallet = pwalletIn;
}

CReserveKey::~CReserveKey()
{
	ReturnKey();
}

void CReserveKey::ReturnKey()
{
	if (nIndex != -1)
	{
		pwallet->ReturnKey(nIndex);
	}

	nIndex = -1;
	vchPubKey = CPubKey();
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey)
{
	if (nIndex == -1)
	{
		CKeyPool keypool;
		
		pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
		
		if (nIndex != -1)
		{
			vchPubKey = keypool.vchPubKey;
		}
		else
		{
			if (pwallet->vchDefaultKey.IsValid())
			{
				LogPrintf("CReserveKey::GetReservedKey(): Warning: Using default key instead of a new key, top up your keypool!");
				
				vchPubKey = pwallet->vchDefaultKey;
			}
			else
			{
				return false;
			}
		}
	}

	assert(vchPubKey.IsValid());

	pubkey = vchPubKey;

	return true;
}

void CReserveKey::KeepKey()
{
	if (nIndex != -1)
	{
		pwallet->KeepKey(nIndex);
	}

	nIndex = -1;
	vchPubKey = CPubKey();
}

