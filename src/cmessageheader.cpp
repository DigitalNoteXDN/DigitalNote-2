#include "compat.h"

#include "serialize.h"
#include "cchainparams.h"
#include "chainparams.h"
#include "util.h"
#include "cdatastream.h"

#include "cmessageheader.h"

CMessageHeader::CMessageHeader()
{
	memcpy(pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE);
	memset(pchCommand, 0, sizeof(pchCommand));
	pchCommand[1] = 1;
	nMessageSize = -1;
	nChecksum = 0;
}

CMessageHeader::CMessageHeader(const char* pszCommand, unsigned int nMessageSizeIn)
{
	memcpy(pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE);
	memset(pchCommand, 0, sizeof(pchCommand));
	strncpy(pchCommand, pszCommand, COMMAND_SIZE);
	nMessageSize = nMessageSizeIn;
	nChecksum = 0;
}

std::string CMessageHeader::GetCommand() const
{
	return std::string(pchCommand, pchCommand + strnlen(pchCommand, COMMAND_SIZE));
}

bool CMessageHeader::IsValid() const
{
	// Check start string
	if (memcmp(pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0)
	{
		return false;
	}

	// Check the command string for errors
	for (const char* p1 = pchCommand; p1 < pchCommand + COMMAND_SIZE; p1++)
	{
		if (*p1 == 0)
		{
			// Must be all zeros after the first zero
			for (; p1 < pchCommand + COMMAND_SIZE; p1++)
			{
				if (*p1 != 0)
				{
					return false;
				}
			}
		}
		else if (*p1 < ' ' || *p1 > 0x7E)
		{
			return false;
		}
	}

	// Message size
	if (nMessageSize > MAX_SIZE)
	{
		LogPrintf("CMessageHeader::IsValid() : (%s, %u bytes) nMessageSize > MAX_SIZE\n", GetCommand(), nMessageSize);
		
		return false;
	}

	return true;
}

unsigned int CMessageHeader::GetSerializeSize(int nType, int nVersion) const
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

	READWRITE(FLATDATA(pchMessageStart));
	READWRITE(FLATDATA(pchCommand));
	READWRITE(nMessageSize);
	READWRITE(nChecksum);

	return nSerSize;
}

template<typename Stream>
void CMessageHeader::Serialize(Stream& s, int nType, int nVersion) const
{
	CSerActionSerialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = true;
	const bool fRead = false;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */

	READWRITE(FLATDATA(pchMessageStart));
	READWRITE(FLATDATA(pchCommand));
	READWRITE(nMessageSize);
	READWRITE(nChecksum);
}

template<typename Stream>
void CMessageHeader::Unserialize(Stream& s, int nType, int nVersion)
{
	CSerActionUnserialize ser_action;
	const bool fGetSize = false;
	const bool fWrite = false;
	const bool fRead = true;
	unsigned int nSerSize = 0;
	assert(fGetSize||fWrite||fRead); /* suppress warning */

	READWRITE(FLATDATA(pchMessageStart));
	READWRITE(FLATDATA(pchCommand));
	READWRITE(nMessageSize);
	READWRITE(nChecksum);
}

template void CMessageHeader::Serialize<CDataStream>(CDataStream& s, int nType, int nVersion) const;
template void CMessageHeader::Unserialize<CDataStream>(CDataStream& s, int nType, int nVersion);

