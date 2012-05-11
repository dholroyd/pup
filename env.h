
#include <stdlib.h>
#include "core_types.h"

void pup_runtime_env_destroy(struct RuntimeEnv *env);
struct RuntimeEnv *pup_runtime_env_create();

struct PupClass *pup_env_get_classobject(ENV);
struct PupClass *pup_env_get_classclass(ENV);
struct PupClass *pup_env_get_classstring(ENV);
struct PupClass *pup_env_get_classexception(ENV);
struct PupClass *pup_env_get_classruntimeerror(ENV);
struct PupObject *pup_env_get_trueinstance(ENV);
struct PupObject *pup_env_get_falseinstance(ENV);
struct PupClass *pup_env_get_classfixnum(ENV);

int pup_env_str_to_sym(ENV, char *str);
const char *pup_env_sym_to_str(ENV, const int sym);

void *pup_alloc_obj(ENV, size_t size);
void *pup_alloc_attr(ENV, size_t size);
void *pup_env_alloc_obj_for_gc_copy(ENV, size_t size);
void *pup_env_alloc_attr_for_gc_copy(ENV, size_t size);
