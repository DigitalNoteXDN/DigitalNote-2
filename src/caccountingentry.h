#ifndef CACCOUNTINGENTRY_H
#define CACCOUNTINGENTRY_H

#include <vector>
#include <string>

#include "types/mapvalue_t.h"

/** Internal transfers.
 * Database key is acentry<account><counter>.
 */
class CAccountingEntry
{
private:
	std::vector<char> _ssExtra;

public:
	std::string strAccount;
	int64_t nCreditDebit;
	int64_t nTime;
	std::string strOtherAccount;
	std::string strComment;
	mapValue_t mapValue;
	int64_t nOrderPos;  // position in ordered transaction list
	uint64_t nEntryNo;

	CAccountingEntry();

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	void SetNull();
};

#endif // CACCOUNTINGENTRY_H
