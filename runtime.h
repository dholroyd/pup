#include "env.h"

#define METH_IMPL(name) \
	struct PupObject *name(ENV, \
                               struct PupObject *target, \
                               long argc, \
                               struct PupObject **argv)

void pup_arity_check(ENV, int expected, int actual);
