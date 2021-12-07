#ifndef THREAD_H
#define THREAD_H

#include "types/ccriticalblock.h"

////////////////////////////////////////////////
//                                            //
// THE SIMPLE DEFINITON, EXCLUDING DEBUG CODE //
//                                            //
////////////////////////////////////////////////

/*
CCriticalSection mutex;
    boost::recursive_mutex mutex;

LOCK(mutex);
    boost::unique_lock<boost::recursive_mutex> criticalblock(mutex);

LOCK2(mutex1, mutex2);
    boost::unique_lock<boost::recursive_mutex> criticalblock1(mutex1);
    boost::unique_lock<boost::recursive_mutex> criticalblock2(mutex2);

TRY_LOCK(mutex, name);
    boost::unique_lock<boost::recursive_mutex> name(mutex, boost::try_to_lock_t);

ENTER_CRITICAL_SECTION(mutex); // no RAII
    mutex.lock();

LEAVE_CRITICAL_SECTION(mutex); // no RAII
    mutex.unlock();
 */

#define LOCK(cs) CCriticalBlock criticalblock(cs, #cs, __FILE__, __LINE__)
#define LOCK2(cs1,cs2) CCriticalBlock criticalblock1(cs1, #cs1, __FILE__, __LINE__),criticalblock2(cs2, #cs2, __FILE__, __LINE__)
#define TRY_LOCK(cs,name) CCriticalBlock name(cs, #cs, __FILE__, __LINE__, true)

#define ENTER_CRITICAL_SECTION(cs) \
    { \
        EnterCritical(#cs, __FILE__, __LINE__, (void*)(&cs)); \
        (cs).lock(); \
    }

#define LEAVE_CRITICAL_SECTION(cs) \
    { \
        (cs).unlock(); \
        LeaveCritical(); \
    }

#define AssertLockHeld(cs) AssertLockHeldInternal(#cs, __FILE__, __LINE__, &cs)

#ifdef DEBUG_LOCKORDER
	void EnterCritical(const char* pszName, const char* pszFile, int nLine, void* cs, bool fTry = false);
	void LeaveCritical();
	std::string LocksHeld();
	void AssertLockHeldInternal(const char* pszName, const char* pszFile, int nLine, void *cs);
#else // DEBUG_LOCKORDER
	void static inline EnterCritical(const char* pszName, const char* pszFile, int nLine, void* cs, bool fTry = false)
	{
		
	}
	
	void static inline LeaveCritical()
	{
		
	}
	
	void static inline AssertLockHeldInternal(const char* pszName, const char* pszFile, int nLine, void *cs)
	{
		
	}
#endif // DEBUG_LOCKORDER

#ifdef DEBUG_LOCKCONTENTION
	void PrintLockContention(const char* pszName, const char* pszFile, int nLine);
#endif // DEBUG_LOCKCONTENTION

#endif // THREAD_H
