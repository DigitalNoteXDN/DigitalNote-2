#ifndef SMSG_CONST_H
#define SMSG_CONST_H

#include "lz4/lz4.h"

const unsigned int SMSG_HDR_LEN = 104;				// length of unencrypted header, 4 + 2 + 1 + 8 + 16 + 33 + 32 + 4 +4
const unsigned int SMSG_PL_HDR_LEN = 1+20+65+4;		// length of encrypted header in payload
const unsigned int SMSG_BUCKET_LEN = 60 * 10;		// in seconds
const unsigned int SMSG_RETENTION = 60 * 60 * 48;	// in seconds
const unsigned int SMSG_SEND_DELAY = 2;				// in seconds, SecureMsgSendData will delay this long between firing
const unsigned int SMSG_THREAD_DELAY = 30;
const unsigned int SMSG_THREAD_LOG_GAP = 6;
const unsigned int SMSG_TIME_LEEWAY = 60;
const unsigned int SMSG_TIME_IGNORE = 90;			// seconds that a peer is ignored for if they fail to deliver messages for a smsgWant
const unsigned int SMSG_MAX_MSG_BYTES = 4096;		// the user input part

// max size of payload worst case compression
const unsigned int SMSG_MAX_MSG_WORST = LZ4_COMPRESSBOUND(SMSG_MAX_MSG_BYTES+SMSG_PL_HDR_LEN);

#endif // SMSG_CONST_H
