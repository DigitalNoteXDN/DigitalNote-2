#ifndef CDIGITALNOTEADDRESS_H
#define CDIGITALNOTEADDRESS_H

#include <string>

#include "cbase58data.h"
#include "types/ctxdestination.h"

class CKeyID;
class CScriptID;

/** base58-encoded DigitalNote addresses.
 * Public-key-hash-addresses have version 0 (or 111 testnet).
 * The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
 * Script-hash-addresses have version 5 (or 196 testnet).
 * The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
 */
class CDigitalNoteAddress : public CBase58Data
{
public:
	CDigitalNoteAddress();
	CDigitalNoteAddress(const CTxDestination &dest);
	CDigitalNoteAddress(const std::string& strAddress);
	CDigitalNoteAddress(const char* pszAddress);

	bool Set(const CKeyID &id);
	bool Set(const CScriptID &id);
	bool Set(const CTxDestination &dest);
	bool IsValid() const;
	CTxDestination Get() const;
	bool GetKeyID(CKeyID &keyID) const;
	bool IsScript() const;
};

#endif // CDIGITALNOTEADDRESS_H
