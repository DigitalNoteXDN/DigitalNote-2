#ifndef CSEMAPHOREGRANT_H
#define CSEMAPHOREGRANT_H

#include "csemaphore.h"

/** RAII-style semaphore lock */
class CSemaphoreGrant
{
private:
	CSemaphore *sem;
	bool fHaveGrant;

public:
	CSemaphoreGrant();
	CSemaphoreGrant(CSemaphore &sema, bool fTry = false);
	~CSemaphoreGrant();

	operator bool();
	void Acquire();
	void Release();
	bool TryAcquire();
	void MoveTo(CSemaphoreGrant &grant);
};

#endif // CSEMAPHOREGRANT_H
