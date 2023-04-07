#ifndef LOCKEDPAGEMANAGER_H
#define LOCKEDPAGEMANAGER_H

#include <boost/thread/once.hpp>

#include "allocators/lockedpagemanagerbase.h"
#include "allocators/memorypagelocker.h"

/**
 * Singleton class to keep track of locked (ie, non-swappable) memory pages, for use in
 * std::allocator templates.
 *
 * Some implementations of the STL allocate memory in some constructors (i.e., see
 * MSVC's vector<T> implementation where it allocates 1 byte of memory in the allocator.)
 * Due to the unpredictable order of static initializers, we have to make sure the
 * LockedPageManager instance exists before any other STL-based objects that use
 * secure_allocator are created. So instead of having LockedPageManager also be
 * static-intialized, it is created on demand.
 */
class LockedPageManager : public LockedPageManagerBase<MemoryPageLocker>
{
private:	
	static LockedPageManager* _instance;
	static boost::once_flag init_flag;

	LockedPageManager();

	static void CreateInstance()
	{
		// Using a local static instance guarantees that the object is initialized
		// when it's first needed and also deinitialized after all objects that use
		// it are done with it.  I can think of one unlikely scenario where we may
		// have a static deinitialization order/problem, but the check in
		// LockedPageManagerBase's destructor helps us detect if that ever happens.
		static LockedPageManager instance;
		
		LockedPageManager::_instance = &instance;
	}

public:
	static LockedPageManager& Instance() 
	{
		boost::call_once(LockedPageManager::CreateInstance, LockedPageManager::init_flag);
		
		return *LockedPageManager::_instance;
	}
};

#endif // LOCKEDPAGEMANAGER_H
