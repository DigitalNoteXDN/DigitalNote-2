#ifndef CUNSIGNEDALERT_H
#define CUNSIGNEDALERT_H

#include <cstdint>
#include <string>
#include <set>

/** Alerts are for notifying old versions if they become too obsolete and
 * need to upgrade.  The message is displayed in the status bar.
 * Alert messages are broadcast as a vector of signed data.  Unserializing may
 * not read the entire buffer if the alert is for a newer version, but older
 * versions can still relay the original data.
 */
class CUnsignedAlert
{
public:
	int nVersion;
	int64_t nRelayUntil;				// when newer nodes stop relaying to newer nodes
	int64_t nExpiration;
	int nID;
	int nCancel;
	std::set<int> setCancel;
	int nMinVer;						// lowest version inclusive
	int nMaxVer;						// highest version inclusive
	std::set<std::string> setSubVer;	// empty matches all
	int nPriority;

	// Actions
	std::string strComment;
	std::string strStatusBar;
	std::string strReserved;

	unsigned int GetSerializeSize(int nType, int nVersion) const;
	template<typename Stream>
	void Serialize(Stream& s, int nType, int nVersion) const;
	template<typename Stream>
	void Unserialize(Stream& s, int nType, int nVersion);

	void SetNull();

	std::string ToString() const;
};

#endif // CUNSIGNEDALERT_H
