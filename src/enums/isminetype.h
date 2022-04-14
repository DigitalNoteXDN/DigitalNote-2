#ifndef ISMINETYPE_H
#define ISMINETYPE_H

/** IsMine() return codes */
enum isminetype
{
	ISMINE_NO = 0,
	ISMINE_WATCH_ONLY = 1,
	ISMINE_SPENDABLE = 2,
	ISMINE_ALL = ISMINE_WATCH_ONLY | ISMINE_SPENDABLE
};

#endif // ISMINETYPE_H
