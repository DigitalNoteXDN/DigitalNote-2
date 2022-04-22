#include "serialize.h"
#include "ctxout.h"
#include "cscriptcompressor.h"
#include "chashwriter.h"
#include "cautofile.h"
#include "cdatastream.h"
#include "cvarint.h"

#include "ctxoutcompressor.h"

CTxOutCompressor::CTxOutCompressor(CTxOut& txoutIn) : txout(txoutIn)
{
	
};

unsigned int CTxOutCompressor::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;

	s.nType = nType;
	s.nVersion = nVersion;
	
	READWRITE(VARINT(txout.nValue));
	CScriptCompressor cscript(REF(txout.scriptPubKey));
	READWRITE(cscript);
	
	return nSerSize;
}

template<typename Stream>
void CTxOutCompressor::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(VARINT(txout.nValue));
	CScriptCompressor cscript(REF(txout.scriptPubKey));
	READWRITE(cscript);
}

template<typename Stream>
void CTxOutCompressor::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(VARINT(txout.nValue));
	CScriptCompressor cscript(REF(txout.scriptPubKey));
	READWRITE(cscript);
}

template void CTxOutCompressor::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CTxOutCompressor::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void CTxOutCompressor::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void CTxOutCompressor::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
template void CTxOutCompressor::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;

