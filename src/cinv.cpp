#include "compat.h"

#include "util.h"
#include "cdatastream.h"
#include "serialize.h"

#include "cinv.h"

static const char* ppszTypeName[] =
{
    "ERROR",
    "tx",
    "dstx",
    "block",
    "filtered block",
    "tx lock request",
    "tx lock vote",
    "spork",
    "masternode winner",
    "unknown",
    "unknown",
    "unknown",
    "unknown",
    "unknown",
    "unknown"
};

CInv::CInv()
{
    type = 0;
    hash = 0;
}

CInv::CInv(int typeIn, const uint256& hashIn)
{
    type = typeIn;
    hash = hashIn;
}

CInv::CInv(const std::string& strType, const uint256& hashIn)
{
    unsigned int i;
	
    for (i = 1; i < ARRAYLEN(ppszTypeName); i++)
    {
        if (strType == ppszTypeName[i])
        {
            type = i;
            
			break;
        }
    }
	
    if (i == ARRAYLEN(ppszTypeName))
	{
        throw std::out_of_range(strprintf("CInv::CInv(std::string, uint256) : unknown type '%s'", strType));
	}
	
    hash = hashIn;
}

bool operator<(const CInv& a, const CInv& b)
{
    return (a.type < b.type || (a.type == b.type && a.hash < b.hash));
}

bool CInv::IsKnownType() const
{
    return (type >= 1 && type < (int)ARRAYLEN(ppszTypeName));
}

const char* CInv::GetCommand() const
{
    if (!IsKnownType())
	{
        throw std::out_of_range(strprintf("CInv::GetCommand() : type=%d unknown type", type));
	}
	
    return ppszTypeName[type];
}

std::string CInv::ToString() const
{
    return strprintf("%s %s", GetCommand(), hash.ToString());
}

unsigned int CInv::GetSerializeSize(int nType, int nVersion) const
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
	
	READWRITE(type);
	READWRITE(hash);
	
	return nSerSize;
}

template<typename Stream>
void CInv::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(type);
	READWRITE(hash);
}

template<typename Stream>
void CInv::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */
	
	READWRITE(type);
	READWRITE(hash);
}

template void CInv::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CInv::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

