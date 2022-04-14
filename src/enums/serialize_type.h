#ifndef SERIALIZE_TYPE_H
#define SERIALIZE_TYPE_H

enum
{
	// primary actions
	SER_NETWORK = (1 << 0),
	SER_DISK = (1 << 1),
	SER_GETHASH = (1 << 2),

	// modifiers
	SER_SKIPSIG = (1 << 16),
	SER_BLOCKHEADERONLY = (1 << 17),
};

#endif // SERIALIZE_TYPE_H
