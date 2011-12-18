
struct RuntimeEnv;

#define ENV struct RuntimeEnv *env

void pup_runtime_env_destroy(struct RuntimeEnv *env);
struct RuntimeEnv *pup_runtime_env_create();

struct PupClass *pup_env_get_classobject(ENV);
struct PupClass *pup_env_get_classclass(ENV);
struct PupClass *pup_env_get_classstring(ENV);
struct PupClass *pup_env_get_classexception(ENV);
struct PupClass *pup_env_get_classruntimeerror(ENV);

int pup_env_str_to_sym(ENV, char *str);
const char *pup_env_sym_to_str(ENV, const int sym);
