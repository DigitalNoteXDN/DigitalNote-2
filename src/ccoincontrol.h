#ifndef CCOINCONTROL_H
#define CCOINCONTROL_H

#include <set>

#include "types/ctxdestination.h"
#include "cnodestination.h"
#include "ckeyid.h"
#include "cscriptid.h"
#include "cstealthaddress.h"

class uint256;
class COutPoint;

/** Coin Control Features. */
class CCoinControl
{
private:
	std::set<COutPoint> setSelected;

public:
	CTxDestination destChange;
	bool useMNengine;
	bool useInstantX;

	CCoinControl();

	void SetNull();
	bool HasSelected() const;
	bool IsSelected(const uint256& hash, unsigned int n) const;
	void Select(COutPoint& output);
	void UnSelect(COutPoint& output);
	void UnSelectAll();
	void ListSelected(std::vector<COutPoint>& vOutpoints);
};

#endif // CCOINCONTROL_H
