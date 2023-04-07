#include "serialize.h"
#include "tinyformat.h"
#include "cdatastream.h"

#include "cunsignedalert.h"

void CUnsignedAlert::SetNull()
{
	nVersion = 1;
	nRelayUntil = 0;
	nExpiration = 0;
	nID = 0;
	nCancel = 0;
	nMinVer = 0;
	nMaxVer = 0;
	nPriority = 0;

	setCancel.clear();
	setSubVer.clear();
	strComment.clear();
	strStatusBar.clear();
	strReserved.clear();
}

std::string CUnsignedAlert::ToString() const
{
	std::string strSetCancel;
	std::string strSetSubVer;

	for(int n : setCancel)
	{
		strSetCancel += strprintf("%d ", n);
	}

	for(std::string str : setSubVer)
	{
		strSetSubVer += "\"" + str + "\" ";
	}

	return strprintf(
		"CAlert(\n"
		"    nVersion     = %d\n"
		"    nRelayUntil  = %d\n"
		"    nExpiration  = %d\n"
		"    nID          = %d\n"
		"    nCancel      = %d\n"
		"    setCancel    = %s\n"
		"    nMinVer      = %d\n"
		"    nMaxVer      = %d\n"
		"    setSubVer    = %s\n"
		"    nPriority    = %d\n"
		"    strComment   = \"%s\"\n"
		"    strStatusBar = \"%s\"\n"
		")\n",
		nVersion,
		nRelayUntil,
		nExpiration,
		nID,
		nCancel,
		strSetCancel,
		nMinVer,
		nMaxVer,
		strSetSubVer,
		nPriority,
		strComment,
		strStatusBar
	);
}

unsigned int CUnsignedAlert::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;

	s.nType = nType;
	s.nVersion = nVersion;

	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(nRelayUntil);
	READWRITE(nExpiration);
	READWRITE(nID);
	READWRITE(nCancel);
	READWRITE(setCancel);
	READWRITE(nMinVer);
	READWRITE(nMaxVer);
	READWRITE(setSubVer);
	READWRITE(nPriority);
	READWRITE(strComment);
	READWRITE(strStatusBar);
	READWRITE(strReserved);

	return nSerSize;
}

template<typename Stream>
void CUnsignedAlert::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	unsigned int nSerSize = 0;

	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(nRelayUntil);
	READWRITE(nExpiration);
	READWRITE(nID);
	READWRITE(nCancel);
	READWRITE(setCancel);
	READWRITE(nMinVer);
	READWRITE(nMaxVer);
	READWRITE(setSubVer);
	READWRITE(nPriority);
	READWRITE(strComment);
	READWRITE(strStatusBar);
	READWRITE(strReserved);
}

template<typename Stream>
void CUnsignedAlert::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	unsigned int nSerSize = 0;

	READWRITE(this->nVersion);
	nVersion = this->nVersion;
	READWRITE(nRelayUntil);
	READWRITE(nExpiration);
	READWRITE(nID);
	READWRITE(nCancel);
	READWRITE(setCancel);
	READWRITE(nMinVer);
	READWRITE(nMaxVer);
	READWRITE(setSubVer);
	READWRITE(nPriority);
	READWRITE(strComment);
	READWRITE(strStatusBar);
	READWRITE(strReserved);
}

template void CUnsignedAlert::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CUnsignedAlert::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

