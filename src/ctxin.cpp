#include "compat.h"

#include "util.h"
#include "serialize.h"
#include "chashwriter.h"
#include "cautofile.h"
#include "cdatastream.h"

#include "ctxin.h"

CTxIn::CTxIn()
{
	nSequence = std::numeric_limits<unsigned int>::max();
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, unsigned int nSequenceIn)
{
	prevout = prevoutIn;
	scriptSig = scriptSigIn;
	nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, unsigned int nOut, CScript scriptSigIn, unsigned int nSequenceIn)
{
	prevout = COutPoint(hashPrevTx, nOut);
	scriptSig = scriptSigIn;
	nSequence = nSequenceIn;
}

unsigned int CTxIn::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;

	s.nType = nType;
	s.nVersion = nVersion;
	
	READWRITE(prevout);
	READWRITE(scriptSig);
	READWRITE(nSequence);
	
	return nSerSize;
}

template<typename Stream>
void CTxIn::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(prevout);
	READWRITE(scriptSig);
	READWRITE(nSequence);
}

template<typename Stream>
void CTxIn::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;
	
	READWRITE(prevout);
	READWRITE(scriptSig);
	READWRITE(nSequence);
}

template void CTxIn::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CTxIn::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);
template void CTxIn::Serialize<CAutoFile>(CAutoFile& s, int nType, int nVersion) const;
template void CTxIn::Unserialize<CAutoFile>(CAutoFile& s, int nType, int nVersion);
template void CTxIn::Serialize<CHashWriter>(CHashWriter& s, int nType, int nVersion) const;

bool CTxIn::IsFinal() const
{
	return (nSequence == std::numeric_limits<unsigned int>::max());
}

bool operator==(const CTxIn& a, const CTxIn& b)
{
	return (a.prevout   == b.prevout &&
			a.scriptSig == b.scriptSig &&
			a.nSequence == b.nSequence);
}

bool operator!=(const CTxIn& a, const CTxIn& b)
{
	return !(a == b);
}

std::string CTxIn::ToString() const
{
	std::string str;

	str += "CTxIn(";
	str += prevout.ToString();

	if (prevout.IsNull())
	{
		str += strprintf(", coinbase %s", HexStr(scriptSig));
	}
	else
	{
		str += strprintf(", scriptSig=%s", scriptSig.ToString().substr(0,24));
	}

	if (nSequence != std::numeric_limits<unsigned int>::max())
	{
		str += strprintf(", nSequence=%u", nSequence);
	}

	str += ")";

	return str;
}

