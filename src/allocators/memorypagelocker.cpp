#include "compat.h"

#include "allocators/memorypagelocker.h"

bool MemoryPageLocker::Lock(const void *addr, size_t len)
{
#ifdef WIN32
	return VirtualLock(const_cast<void*>(addr), len);
#else
	return mlock(addr, len) == 0;
#endif
}

bool MemoryPageLocker::Unlock(const void *addr, size_t len)
{
#ifdef WIN32
	return VirtualUnlock(const_cast<void*>(addr), len);
#else
	return munlock(addr, len) == 0;
#endif
}

