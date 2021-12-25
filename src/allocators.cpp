#include "support/cleanse.h"
#include "allocators/lockedpagemanager.h"

#include "allocators.h"

LockedPageManager* LockedPageManager::_instance = NULL;
boost::once_flag LockedPageManager::init_flag = BOOST_ONCE_INIT;

//
// Functions for directly locking/unlocking memory objects.
// Intended for non-dynamically allocated structures.
//
template<typename T>
void LockObject(const T &t)
{
    LockedPageManager::Instance().LockRange((void*)(&t), sizeof(T));
}

template<typename T>
void UnlockObject(const T &t)
{
    memory_cleanse((void*)(&t), sizeof(T));
	
    LockedPageManager::Instance().UnlockRange((void*)(&t), sizeof(T));
}

template void LockObject<unsigned char>(const unsigned char &t);
template void UnlockObject<unsigned char>(const unsigned char &t);
template void LockObject<unsigned char [32]>(unsigned char const (&) [32]);
template void UnlockObject<unsigned char [32]>(unsigned char const (&) [32]);
template void LockObject<unsigned char [64]>(unsigned char const (&) [64]);
template void UnlockObject<unsigned char [64]>(unsigned char const (&) [64]);