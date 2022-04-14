#ifndef CDIGITALBOTEEXTKETBASE_H
#define CDIGITALBOTEEXTKETBASE_H

#include "enums/cchainparams_base58type.h"
#include "cbase58data.h"
#include "cchainparams.h"
#include "chainparams.h"

template<typename K, int Size, CChainParams_Base58Type Type>
class CDigitalNoteExtKeyBase : public CBase58Data
{
public:
	void SetKey(const K &key)
	{
		unsigned char vch[Size];
		
		key.Encode(vch);
		
		SetData(Params().Base58Prefix(Type), vch, vch+Size);
	}
	
	CDigitalNoteExtKeyBase()
	{
		
	}
	
	CDigitalNoteExtKeyBase(const K &key)
	{
		SetKey(key);
	}
	
	K GetKey()
	{
		K ret;
		
		ret.Decode(&vchData[0], &vchData[Size]);
		
		return ret;
	}
};

//typedef CDigitalNoteExtKeyBase<CExtKey, 74, CChainParams::EXT_SECRET_KEY> CDigitalNoteExtKey;
//typedef CDigitalNoteExtKeyBase<CExtPubKey, 74, CChainParams::EXT_PUBLIC_KEY> CDigitalNoteExtPubKey;

#endif // CDIGITALBOTEEXTKETBASE_H
