#ifndef LOCKEDPAGEMANAGERBASE_H
#define LOCKEDPAGEMANAGERBASE_H

#include <map>
#include <boost/thread/mutex.hpp>

/**
 * Thread-safe class to keep track of locked (ie, non-swappable) memory pages.
 *
 * Memory locks do not stack, that is, pages which have been locked several times by calls to mlock()
 * will be unlocked by a single call to munlock(). This can result in keying material ending up in swap when
 * those functions are used naively. This class simulates stacking memory locks by keeping a counter per page.
 *
 * @note By using a map from each page base address to lock count, this class is optimized for
 * small objects that span up to a few pages, mostly smaller than a page. To support large allocations,
 * something like an interval tree would be the preferred data structure.
 */
template <class Locker>
class LockedPageManagerBase
{
private:
	typedef std::map<size_t, int> Histogram;

	Locker locker;
	boost::mutex mutex;
	size_t page_size, page_mask;
	// map of page base address to lock count
	Histogram histogram;

public:
	LockedPageManagerBase(size_t page_size);
	~LockedPageManagerBase();

	// For all pages in affected range, increase lock count
	void LockRange(void *p, size_t size);
	void UnlockRange(void *p, size_t size);
	int GetLockedPageCount();
};

#endif // LOCKEDPAGEMANAGERBASE_H
