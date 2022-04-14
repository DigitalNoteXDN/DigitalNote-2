#ifndef CSEMAPHORE_H
#define CSEMAPHORE_H

#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>

class CSemaphore
{
private:
	boost::condition_variable condition;
	boost::mutex mutex;
	int value;

public:
	CSemaphore(int init);

	void wait();
	bool try_wait();
	void post();
};

#endif // CSEMAPHORE_H
