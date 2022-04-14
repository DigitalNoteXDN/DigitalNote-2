#ifndef ANNOTATEDMIXIN_H
#define ANNOTATEDMIXIN_H

#include "thread/safety.h"

// Template mixin that adds -Wthread-safety locking annotations to a
// subset of the mutex API.
template <typename PARENT>
class LOCKABLE AnnotatedMixin : public PARENT
{
public:
	void lock() EXCLUSIVE_LOCK_FUNCTION()
	{
		PARENT::lock();
	}

	void unlock() UNLOCK_FUNCTION()
	{
		PARENT::unlock();
	}

	bool try_lock() EXCLUSIVE_TRYLOCK_FUNCTION(true)
	{
		return PARENT::try_lock();
	}
};

#endif // ANNOTATEDMIXIN_H
