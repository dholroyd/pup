
void pup_raise_runtimeerror(char *message);

struct Object *pup_new_runtimeerrorf(const char *messagefmt, ...);

const char *exception_text(struct Object *ex);
