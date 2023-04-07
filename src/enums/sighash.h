#ifndef SIGHASH_H
#define SIGHASH_H

/** Signature hash types/flags */
enum
{
	SIGHASH_ALL = 1,
	SIGHASH_NONE = 2,
	SIGHASH_SINGLE = 3,
	SIGHASH_ANYONECANPAY = 0x80,
};

#endif // SIGHASH_H
