#ifndef CMESSAGEHEADER_H
#define CMESSAGEHEADER_H

#include <string>

#include "message_start_size.h"

/** Message header.
 * (4) message start.
 * (12) command.
 * (4) size.
 * (4) checksum.
 */
class CMessageHeader
{
public:
	// TODO: make private (improves encapsulation)
	enum
	{
		COMMAND_SIZE=12,
		MESSAGE_SIZE_SIZE=sizeof(int),
		CHECKSUM_SIZE=sizeof(int),
		MESSAGE_SIZE_OFFSET=MESSAGE_START_SIZE+COMMAND_SIZE,
		CHECKSUM_OFFSET=MESSAGE_SIZE_OFFSET+MESSAGE_SIZE_SIZE,
		HEADER_SIZE=MESSAGE_START_SIZE+COMMAND_SIZE+MESSAGE_SIZE_SIZE+CHECKSUM_SIZE
	};

	char pchMessageStart[MESSAGE_START_SIZE];
	char pchCommand[COMMAND_SIZE];
	unsigned int nMessageSize;
	unsigned int nChecksum;

	CMessageHeader();
	CMessageHeader(const char* pszCommand, unsigned int nMessageSizeIn);

	std::string GetCommand() const;
	bool IsValid() const;

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);
};

#endif // CMESSAGEHEADER_H
