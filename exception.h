
void pup_raise_runtimeerror(char *message);

struct PupObject *pup_new_runtimeerrorf(const char *messagefmt, ...);

const char *exception_text(struct PupObject *ex);

bool pup_instanceof_exception(struct PupObject *obj);
