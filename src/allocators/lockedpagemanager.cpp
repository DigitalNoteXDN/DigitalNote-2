#include "compat.h"
#include "allocators/memorypagelocker.h"

#include "allocators/lockedpagemanager.h"

/** Determine system page size in bytes */
static inline size_t GetSystemPageSize()
{
	size_t page_size;

#if defined(WIN32)
	SYSTEM_INFO sSysInfo;
	GetSystemInfo(&sSysInfo);
	page_size = sSysInfo.dwPageSize;
#elif defined(PAGESIZE) // defined in limits.h
	page_size = PAGESIZE;
#else // assume some POSIX OS
	page_size = sysconf(_SC_PAGESIZE);
#endif

	return page_size;
}

LockedPageManager::LockedPageManager() : LockedPageManagerBase<MemoryPageLocker>(GetSystemPageSize())
{
	
}

