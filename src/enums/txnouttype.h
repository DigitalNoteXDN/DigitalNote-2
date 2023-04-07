#ifndef TXNOUTTYPE_H
#define TXNOUTTYPE_H

enum txnouttype
{
	TX_NONSTANDARD,
	// 'standard' transaction types:
	TX_PUBKEY,
	TX_PUBKEYHASH,
	TX_SCRIPTHASH,
	TX_MULTISIG,
	TX_NULL_DATA,
};

#endif // TXNOUTTYPE_H
