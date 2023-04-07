#ifndef CDIGITALNOTESECRET_H
#define CDIGITALNOTESECRET_H

#include <vector>

#include "cbase58data.h"

class CKey;

/**
 * A base58-encoded secret key
 */
class CDigitalNoteSecret : public CBase58Data
{
public:
	CDigitalNoteSecret();
	CDigitalNoteSecret(const CKey& vchSecret);

	void SetKey(const CKey& vchSecret);
	CKey GetKey();
	bool IsValid() const;
	bool SetString(const char* pszSecret);
	bool SetString(const std::string& strSecret);
};

#endif // CDIGITALNOTESECRET_H
