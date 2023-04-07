#ifndef NET_SECMSGNODE_H
#define NET_SECMSGNODE_H

#include "types/ccriticalsection.h"

/** Information about a DigitalNote (D-Note) peer */
class SecMsgNode
{
public:
	CCriticalSection cs_smsg_net;
	int64_t lastSeen;
	int64_t lastMatched;
	int64_t ignoreUntil;
	uint32_t nWakeCounter;
	uint32_t nPeerId;
	bool fEnabled;

	SecMsgNode()
	{
		lastSeen = 0;
		lastMatched = 0;
		ignoreUntil = 0;
		nWakeCounter = 0;
		nPeerId = 0;
		fEnabled = false;
	}

	~SecMsgNode()
	{
		
	}
};

#endif // NET_SECMSGNODE_H
