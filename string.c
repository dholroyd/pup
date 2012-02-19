
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "core_types.h"
#include "runtime.h"
#include "raise.h"
#include "exception.h"
#include "object.h"
#include "class.h"

struct PupString {
	struct PupObject obj_header;
	char *value;
};


static struct PupObject *allocate_instance(ENV, struct PupClass *type)
{
	struct PupObject *obj =
		(struct PupObject *)pup_alloc(env, sizeof(struct PupString));
	obj_init(obj, type);
	struct PupString *str = (struct PupString *)obj;
	str->value = NULL;
	return obj;
}


struct PupClass *pup_bootstrap_create_classstring(ENV)
{
	return pup_internal_create_class(env,
	                        pup_env_get_classobject(env),
	                        NULL,  // no lexical scope
	                        "String",
	                        &allocate_instance);
}

struct PupObject *pup_string_new_cstr(ENV, const char *str)
{
	struct PupString *string = malloc(sizeof(struct PupString));
	if (!string) {
		return NULL;
	}
	obj_init(&(string->obj_header), pup_env_get_classstring(env));
	string->value = strdup(str);
	return (struct PupObject *)string;
}

const char *pup_string_value_unsafe(struct PupObject *str)
{
	return ((struct PupString *)str)->value;
}

bool pup_is_string(ENV, struct PupObject *obj)
{
	return pup_object_instanceof(obj, pup_env_get_classstring(env));
}

const char *pup_string_value(ENV, struct PupObject *str)
{
	if (!pup_is_string(env, str)) {
		pup_raise(pup_new_runtimeerrorf(env, "String expected, %s given",
		                                pup_object_type_name(str)));
	}
	return pup_string_value_unsafe(str);
}
