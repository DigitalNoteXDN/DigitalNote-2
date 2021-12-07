#include "thread.h"
#include "cscript.h"
#include "ckey.h"
#include "cpubkey.h"
#include "uint/uint256.h"
#include "ckeyid.h"
#include "cscriptid.h"

#include "ccryptokeystore.h"

CCryptoKeyStore::CCryptoKeyStore() : fUseCrypto(false)
{
	
}

bool CCryptoKeyStore::IsCrypted() const
{
	return fUseCrypto;
}

bool CCryptoKeyStore::IsLocked() const
{
	if (!IsCrypted())
	{
		return false;
	}
	
	bool result;
	
	{
		LOCK(cs_KeyStore);
		
		result = vMasterKey.empty();
	}
	
	return result;
}

bool CCryptoKeyStore::LockKeyStore()
{
    if (!SetCrypted())
	{
        return false;
	}
	
    {
        LOCK(cs_KeyStore);
        
		vMasterKey.clear();
    }

    NotifyStatusChanged(this);
    
	return true;
}

bool CCryptoKeyStore::AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    {
        LOCK(cs_KeyStore);
		
        if (!SetCrypted())
		{
            return false;
		}
		
        mapCryptedKeys[vchPubKey.GetID()] = std::make_pair(vchPubKey, vchCryptedSecret);
    }
	
    return true;
}

bool CCryptoKeyStore::AddKeyPubKey(const CKey& key, const CPubKey &pubkey)
{
    {
        LOCK(cs_KeyStore);
		
        if (!IsCrypted())
		{
            return CBasicKeyStore::AddKeyPubKey(key, pubkey);
		}
		
        if (IsLocked())
		{
            return false;
		}
		
        std::vector<unsigned char> vchCryptedSecret;
        CKeyingMaterial vchSecret(key.begin(), key.end());
        
		if (!EncryptSecret(vMasterKey, vchSecret, pubkey.GetHash(), vchCryptedSecret))
		{
            return false;
		}
		
        if (!AddCryptedKey(pubkey, vchCryptedSecret))
		{
            return false;
		}
    }
	
    return true;
}

bool CCryptoKeyStore::HaveKey(const CKeyID &address) const
{
	{
		LOCK(cs_KeyStore);
		
		if (!IsCrypted())
		{
			return CBasicKeyStore::HaveKey(address);
		}
		
		return mapCryptedKeys.count(address) > 0;
	}
	
	return false;
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, CKey& keyOut) const
{
    {
        LOCK(cs_KeyStore);
		
        if (!IsCrypted())
		{
            return CBasicKeyStore::GetKey(address, keyOut);
		}
		
        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        
		if (mi != mapCryptedKeys.end())
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            CKeyingMaterial vchSecret;
            
			if (!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
			{
                return false;
			}
			
            if (vchSecret.size() != 32)
			{
                return false;
			}
			
            keyOut.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
            
			return true;
        }
    }
	
    return false;
}

bool CCryptoKeyStore::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    {
        LOCK(cs_KeyStore);
		
        if (!IsCrypted())
		{
            return CKeyStore::GetPubKey(address, vchPubKeyOut);
		}
		
        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        
		if (mi != mapCryptedKeys.end())
        {
            vchPubKeyOut = (*mi).second.first;
            
			return true;
        }
    }
	
    return false;
}

void CCryptoKeyStore::GetKeys(std::set<CKeyID> &setAddress) const
{
	if (!IsCrypted())
	{
		CBasicKeyStore::GetKeys(setAddress);
		
		return;
	}
	
	setAddress.clear();
	
	for (CryptedKeyMap::const_iterator mi = mapCryptedKeys.begin(); mi != mapCryptedKeys.end(); mi++)
	{
		setAddress.insert((*mi).first);
	}
}

/*
	Private
*/
bool CCryptoKeyStore::SetCrypted()
{
    LOCK(cs_KeyStore);
	
    if (fUseCrypto)
	{
        return true;
    }
	
	if (!mapKeys.empty())
	{
        return false;
	}
	
    fUseCrypto = true;
    
	return true;
}

bool CCryptoKeyStore::EncryptKeys(CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        
		if (!mapCryptedKeys.empty() || IsCrypted())
		{
            return false;
		}
		
        fUseCrypto = true;
		
        for(KeyMap::value_type& mKey : mapKeys)
        {
            const CKey &key = mKey.second;
            CPubKey vchPubKey = key.GetPubKey();
            CKeyingMaterial vchSecret(key.begin(), key.end());
            std::vector<unsigned char> vchCryptedSecret;
            
			if (!EncryptSecret(vMasterKeyIn, vchSecret, vchPubKey.GetHash(), vchCryptedSecret))
			{
                return false;
			}
			
            if (!AddCryptedKey(vchPubKey, vchCryptedSecret))
			{
                return false;
			}
        }
        
		mapKeys.clear();
    }
	
    return true;
}

bool CCryptoKeyStore::Unlock(const CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        
		if (!SetCrypted())
		{
            return false;
		}
		
		for (CryptedKeyMap::const_iterator mi = mapCryptedKeys.begin(); mi != mapCryptedKeys.end(); mi++)
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            CKeyingMaterial vchSecret;
			
            if(!DecryptSecret(vMasterKeyIn, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
			{
                return false;
            }
			
			if (vchSecret.size() != 32)
			{
                return false;
			}
			
            CKey key;
            
			key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
            
			if (key.GetPubKey() == vchPubKey)
			{
                break;
			}
			
            return false;
        }
		
        vMasterKey = vMasterKeyIn;
    }
    
	NotifyStatusChanged(this);
	
    return true;
}

