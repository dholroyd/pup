
void pup_raise_runtimeerror(ENV, char *message);

struct PupObject *pup_new_runtimeerrorf(ENV, const char *messagefmt, ...);

const char *exception_text(ENV, struct PupObject *ex);

bool pup_instanceof_exception(ENV, struct PupObject *obj);

// TODO move to Kernel class
METH_IMPL(pup_object_raise);

/*
 * Bootstrap the Exception class. Used while initialising runtime environment
 */
void pup_exception_class_init(ENV, struct PupClass *class_excep);
