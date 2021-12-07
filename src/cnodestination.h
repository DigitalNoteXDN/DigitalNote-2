#ifndef CNODESTINATION_H
#define CNODESTINATION_H

class CNoDestination
{
public:
    friend bool operator==(const CNoDestination &a, const CNoDestination &b)
	{
		return true;
	}
	
    friend bool operator<(const CNoDestination &a, const CNoDestination &b)
	{
		return true;
	}
};

#endif // CNODESTINATION_H
