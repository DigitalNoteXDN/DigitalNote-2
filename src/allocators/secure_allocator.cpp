#include "allocators/lockedpagemanager.h"
#include "support/cleanse.h"

#include "secure_allocator.h"

template<typename T>
secure_allocator<T>::secure_allocator() throw()
{
	
}

template<typename T>
secure_allocator<T>::~secure_allocator() throw()
{
	
}

template<typename T>
T* secure_allocator<T>::allocate(std::size_t n, const void *hint)
{
	T *p;
	
	p = std::allocator<T>::allocate(n, hint);
	
	if (p != NULL)
	{
		LockedPageManager::Instance().LockRange(p, sizeof(T) * n);
	}
	
	return p;
}

template<typename T>
void secure_allocator<T>::deallocate(T* p, std::size_t n)
{
	if (p != NULL)
	{
		memory_cleanse(p, sizeof(T) * n);
		
		LockedPageManager::Instance().UnlockRange(p, sizeof(T) * n);
	}
	
	std::allocator<T>::deallocate(p, n);
}

template struct secure_allocator<char>;
template struct secure_allocator<unsigned char>;
