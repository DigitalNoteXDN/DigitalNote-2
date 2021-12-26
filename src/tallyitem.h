#ifndef TALLYITEM_H
#define TALLYITEM_H

#include <vector>
#include <limits>

#include "types/camount.h"

class uint256;

struct tallyitem
{
	CAmount nAmount;
	int nConf;
	int nBCConf;
	std::vector<uint256> txids;
	bool fIsWatchonly;

	tallyitem()
	{
		nAmount = 0;
		nConf = std::numeric_limits<int>::max();
		nBCConf = std::numeric_limits<int>::max();
		fIsWatchonly = false;
	}
};

#endif // TALLYITEM_H
