#define METH_IMPL(name) struct PupObject *name(struct PupObject *target, \
                                               long argc,             \
                                               struct PupObject **argv)

void pup_arity_check(int expected, int actual);
