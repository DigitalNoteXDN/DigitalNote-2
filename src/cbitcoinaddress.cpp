#include "cchainparams.h"
#include "chainparams.h"
#include "cbitcoinaddressvisitor.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"

#include "cbitcoinaddress.h"

/** base58-encoded Bitcoin addresses.
 * Public-key-hash-addresses have version 0 (or 111 testnet).
 * The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
 * Script-hash-addresses have version 5 (or 196 testnet).
 * The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
 */
CChainParams_Base58Type pubkey_address = (CChainParams_Base58Type)0;
CChainParams_Base58Type script_address = (CChainParams_Base58Type)5;

CBitcoinAddress::CBitcoinAddress()
{
	
}

CBitcoinAddress::CBitcoinAddress(const CTxDestination &dest)
{
	Set(dest);
}

CBitcoinAddress::CBitcoinAddress(const std::string& strAddress)
{
	SetString(strAddress);
}

CBitcoinAddress::CBitcoinAddress(const char* pszAddress)
{
	SetString(pszAddress);
}

bool CBitcoinAddress::Set(const CKeyID &id)
{
	SetData(Params().Base58Prefix(pubkey_address), &id, 20);

	return true;
}

bool CBitcoinAddress::Set(const CScriptID &id)
{
	SetData(Params().Base58Prefix(script_address), &id, 20);

	return true;
}

bool CBitcoinAddress::Set(const CTxDestination &dest)
{
	return boost::apply_visitor(CBitcoinAddressVisitor(this), dest);
}

bool CBitcoinAddress::IsValid() const
{
	bool fCorrectSize = vchData.size() == 20;
	bool fKnownVersion = vchVersion == Params().Base58Prefix(pubkey_address) ||
						 vchVersion == Params().Base58Prefix(script_address);

	return fCorrectSize && fKnownVersion;
}

CTxDestination CBitcoinAddress::Get() const
{
	if (!IsValid())
	{
		return CNoDestination();
	}

	uint160 id;
	memcpy(&id, &vchData[0], 20);

	if (vchVersion == Params().Base58Prefix(pubkey_address))
	{
		return CKeyID(id);
	}
	else if (vchVersion == Params().Base58Prefix(script_address))
	{
		return CScriptID(id);
	}
	else
	{
		return CNoDestination();
	}
}

bool CBitcoinAddress::GetKeyID(CKeyID &keyID) const
{
	if (!IsValid() || vchVersion != Params().Base58Prefix(pubkey_address))
	{
		return false;
	}

	uint160 id;

	memcpy(&id, &vchData[0], 20);
	keyID = CKeyID(id);

	return true;
}

bool CBitcoinAddress::IsScript() const
{
	return IsValid() && vchVersion == Params().Base58Prefix(script_address);
}

