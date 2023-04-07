#ifndef ECCVERIFYHANDLE_H
#define ECCVERIFYHANDLE_H

/** Users of this module must hold an ECCVerifyHandle. The constructor and
 *  destructor of these are not allowed to run in parallel, though. */
class ECCVerifyHandle
{
	static int refcount;

public:
	ECCVerifyHandle();
	~ECCVerifyHandle();
};

#endif // ECCVERIFYHANDLE_H
