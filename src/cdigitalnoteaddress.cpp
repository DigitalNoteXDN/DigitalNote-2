#include "cchainparams.h"
#include "chainparams.h"
#include "cdigitalnoteaddressvisitor.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"

#include "cdigitalnoteaddress.h"

CDigitalNoteAddress::CDigitalNoteAddress()
{
	
}

CDigitalNoteAddress::CDigitalNoteAddress(const CTxDestination &dest)
{
	Set(dest);
}

CDigitalNoteAddress::CDigitalNoteAddress(const std::string& strAddress)
{
	SetString(strAddress);
}

CDigitalNoteAddress::CDigitalNoteAddress(const char* pszAddress)
{
	SetString(pszAddress);
}

bool CDigitalNoteAddress::Set(const CKeyID &id)
{
    SetData(Params().Base58Prefix(CChainParams_Base58Type::PUBKEY_ADDRESS), &id, 20);
	
    return true;
}

bool CDigitalNoteAddress::Set(const CScriptID &id)
{
    SetData(Params().Base58Prefix(CChainParams_Base58Type::SCRIPT_ADDRESS), &id, 20);
	
    return true;
}

bool CDigitalNoteAddress::Set(const CTxDestination &dest)
{
    return boost::apply_visitor(CDigitalNoteAddressVisitor(this), dest);
}

bool CDigitalNoteAddress::IsValid() const
{
    bool fCorrectSize = vchData.size() == 20;
    bool fKnownVersion = vchVersion == Params().Base58Prefix(CChainParams_Base58Type::PUBKEY_ADDRESS) ||
                         vchVersion == Params().Base58Prefix(CChainParams_Base58Type::SCRIPT_ADDRESS);
    
	return fCorrectSize && fKnownVersion;
}

CTxDestination CDigitalNoteAddress::Get() const
{
    if (!IsValid())
	{
        return CNoDestination();
	}
	
    uint160 id;
    
	memcpy(&id, &vchData[0], 20);
    
	if (vchVersion == Params().Base58Prefix(CChainParams_Base58Type::PUBKEY_ADDRESS))
	{
        return CKeyID(id);
	}
    else if (vchVersion == Params().Base58Prefix(CChainParams_Base58Type::SCRIPT_ADDRESS))
	{
        return CScriptID(id);
	}
    else
	{
        return CNoDestination();
	}
}

bool CDigitalNoteAddress::GetKeyID(CKeyID &keyID) const
{
    uint160 id;
	
    if (!IsValid() || vchVersion != Params().Base58Prefix(CChainParams_Base58Type::PUBKEY_ADDRESS))
	{
        return false;
	}
	
    memcpy(&id, &vchData[0], 20);
    
	keyID = CKeyID(id);
    
	return true;
}

bool CDigitalNoteAddress::IsScript() const
{
    return IsValid() && vchVersion == Params().Base58Prefix(CChainParams_Base58Type::SCRIPT_ADDRESS);
}

