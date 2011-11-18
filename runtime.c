
#include "object.h"
#include "raise.h"
#include "exception.h"


void pup_arity_check(int expected, int actual)
{
	if (expected != actual) {
		// TODO: ArgumentError
		pup_raise(pup_new_runtimeerrorf("wrong number of arguments (%d for %d)",
		                        actual, expected));
	}
}
