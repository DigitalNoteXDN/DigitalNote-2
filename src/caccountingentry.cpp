#include "serialize.h"
#include "enums/serialize_type.h"
#include "cwallettx.h"
#include "cdatastream.h"

#include "caccountingentry.h"

CAccountingEntry::CAccountingEntry()
{
	SetNull();
}

unsigned int CAccountingEntry::GetSerializeSize(int nType, int nVersion) const
{
	CSerActionGetSerializeSize ser_action;
	const bool fGetSize = true;
	const bool fWrite = false;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	ser_streamplaceholder s;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	s.nType = nType;
	s.nVersion = nVersion;
	
	CAccountingEntry& me = *const_cast<CAccountingEntry*>(this);
	
	if (!(nType & SER_GETHASH))
	{
		READWRITE(nVersion);
	}
	
	// Note: strAccount is serialized as part of the key, not here.
	READWRITE(nCreditDebit);
	READWRITE(nTime);
	READWRITE(strOtherAccount);

	if (!fRead)
	{
		WriteOrderPos(nOrderPos, me.mapValue);

		if (!(mapValue.empty() && _ssExtra.empty()))
		{
			CDataStream ss(nType, nVersion);
			
			ss.insert(ss.begin(), '\0');
			ss << mapValue;
			ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
			
			me.strComment.append(ss.str());
		}
	}

	READWRITE(strComment);

	size_t nSepPos = strComment.find("\0", 0, 1);
	
	if (fRead)
	{
		me.mapValue.clear();
		
		if (std::string::npos != nSepPos)
		{
			CDataStream ss(std::vector<char>(strComment.begin() + nSepPos + 1, strComment.end()), nType, nVersion);
			
			ss >> me.mapValue;
			
			me._ssExtra = std::vector<char>(ss.begin(), ss.end());
		}
		
		ReadOrderPos(me.nOrderPos, me.mapValue);
	}
	
	if (std::string::npos != nSepPos)
	{
		me.strComment.erase(nSepPos);
	}
	
	me.mapValue.erase("n");
	
	return nSerSize;
}

template<typename Stream>
void CAccountingEntry::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	CAccountingEntry& me = *const_cast<CAccountingEntry*>(this);
	
	if (!(nType & SER_GETHASH))
	{
		READWRITE(nVersion);
	}
	
	// Note: strAccount is serialized as part of the key, not here.
	READWRITE(nCreditDebit);
	READWRITE(nTime);
	READWRITE(strOtherAccount);

	if (!fRead)
	{
		WriteOrderPos(nOrderPos, me.mapValue);

		if (!(mapValue.empty() && _ssExtra.empty()))
		{
			CDataStream ss(nType, nVersion);
			
			ss.insert(ss.begin(), '\0');
			ss << mapValue;
			ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
			
			me.strComment.append(ss.str());
		}
	}

	READWRITE(strComment);

	size_t nSepPos = strComment.find("\0", 0, 1);
	
	if (fRead)
	{
		me.mapValue.clear();
		
		if (std::string::npos != nSepPos)
		{
			CDataStream ss(std::vector<char>(strComment.begin() + nSepPos + 1, strComment.end()), nType, nVersion);
			
			ss >> me.mapValue;
			
			me._ssExtra = std::vector<char>(ss.begin(), ss.end());
		}
		
		ReadOrderPos(me.nOrderPos, me.mapValue);
	}
	
	if (std::string::npos != nSepPos)
	{
		me.strComment.erase(nSepPos);
	}
	
	me.mapValue.erase("n");
}

template<typename Stream>
void CAccountingEntry::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	CAccountingEntry& me = *const_cast<CAccountingEntry*>(this);
	
	if (!(nType & SER_GETHASH))
	{
		READWRITE(nVersion);
	}
	
	// Note: strAccount is serialized as part of the key, not here.
	READWRITE(nCreditDebit);
	READWRITE(nTime);
	READWRITE(strOtherAccount);

	if (!fRead)
	{
		WriteOrderPos(nOrderPos, me.mapValue);

		if (!(mapValue.empty() && _ssExtra.empty()))
		{
			CDataStream ss(nType, nVersion);
			
			ss.insert(ss.begin(), '\0');
			ss << mapValue;
			ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
			
			me.strComment.append(ss.str());
		}
	}

	READWRITE(strComment);

	size_t nSepPos = strComment.find("\0", 0, 1);
	
	if (fRead)
	{
		me.mapValue.clear();
		
		if (std::string::npos != nSepPos)
		{
			CDataStream ss(std::vector<char>(strComment.begin() + nSepPos + 1, strComment.end()), nType, nVersion);
			
			ss >> me.mapValue;
			
			me._ssExtra = std::vector<char>(ss.begin(), ss.end());
		}
		
		ReadOrderPos(me.nOrderPos, me.mapValue);
	}
	
	if (std::string::npos != nSepPos)
	{
		me.strComment.erase(nSepPos);
	}
	
	me.mapValue.erase("n");
}

template void CAccountingEntry::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CAccountingEntry::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

void CAccountingEntry::SetNull()
{
	nCreditDebit = 0;
	nTime = 0;
	strAccount.clear();
	strOtherAccount.clear();
	strComment.clear();
	nOrderPos = -1;
}

