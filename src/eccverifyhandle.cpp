#include <cassert>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include "secp256k1_context_verify.h"

#include "eccverifyhandle.h"

/* static */
int ECCVerifyHandle::refcount = 0;

ECCVerifyHandle::ECCVerifyHandle()
{
	if (refcount == 0)
	{
		assert(secp256k1_context_verify == NULL);
		
		secp256k1_context_verify = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
		
		assert(secp256k1_context_verify != NULL);
	}

	refcount++;
}

ECCVerifyHandle::~ECCVerifyHandle()
{
	refcount--;

	if (refcount == 0)
	{
		assert(secp256k1_context_verify != NULL);
		
		secp256k1_context_destroy(secp256k1_context_verify);
		secp256k1_context_verify = NULL;
	}
}
