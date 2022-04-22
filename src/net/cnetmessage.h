#ifndef CNETMESSAGE_H
#define CNETMESSAGE_H

#include "cmessageheader.h"
#include "cdatastream.h"

class CNetMessage
{
public:
	bool in_data;				// parsing header (false) or data (true)
	CDataStream hdrbuf;			// partially received header
	CMessageHeader hdr;			// complete header
	unsigned int nHdrPos;
	CDataStream vRecv;			// received message data
	unsigned int nDataPos;

	CNetMessage(int nTypeIn, int nVersionIn);
	
	bool complete() const;
	void SetVersion(int nVersionIn);
	int readHeader(const char *pch, unsigned int nBytes);
	int readData(const char *pch, unsigned int nBytes);
};

#endif // CNETMESSAGE_H
