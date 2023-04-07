#ifndef COMPAREBYPRIORITY_H
#define COMPAREBYPRIORITY_H

#include "coutput.h"

struct CompareByPriority
{
	bool operator()(const COutput& t1, const COutput& t2) const
	{
		return t1.Priority() > t2.Priority();
	}
};

#endif // COMPAREBYPRIORITY_H
