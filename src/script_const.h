#ifndef SCRIPT_CONST_H
#define SCRIPT_CONST_H

#include "enums/script_verify.h"

static const unsigned int MAX_SCRIPT_ELEMENT_SIZE = 520;	// bytes
static const unsigned int MAX_OP_RETURN_RELAY = 40;			// bytes

// Mandatory script verification flags that all new blocks must comply with for
// them to be valid. (but old blocks may not comply with)
//
// Failing one of these tests may trigger a DoS ban - see ConnectInputs() for
// details.
static const unsigned int MANDATORY_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_NONE;

// Standard script verification flags that standard transactions will comply
// with. However scripts violating these flags may still be present in valid
// blocks and we must accept those blocks.
static const unsigned int STANDARD_SCRIPT_VERIFY_FLAGS = MANDATORY_SCRIPT_VERIFY_FLAGS |
														SCRIPT_VERIFY_STRICTENC |
														SCRIPT_VERIFY_NULLDUMMY |
														SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS;

// For convenience, standard but not mandatory verify flags.
static const unsigned int STANDARD_NOT_MANDATORY_VERIFY_FLAGS = STANDARD_SCRIPT_VERIFY_FLAGS & ~MANDATORY_SCRIPT_VERIFY_FLAGS;

#endif // SCRIPT_CONST_H
