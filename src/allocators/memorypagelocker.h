#ifndef MEMORYPAGELOCKER_H
#define MEMORYPAGELOCKER_H

#include <stddef.h>

/**
 * OS-dependent memory page locking/unlocking.
 * Defined as policy class to make stubbing for test possible.
 */
class MemoryPageLocker
{
public:
	/** Lock memory pages.
	 * addr and len must be a multiple of the system page size
	 */
	bool Lock(const void *addr, size_t len);
	/** Unlock memory pages.
	 * addr and len must be a multiple of the system page size
	 */
	bool Unlock(const void *addr, size_t len);
};

#endif // MEMORYPAGELOCKER_H
