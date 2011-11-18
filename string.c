
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "core_types.h"
#include "runtime.h"
#include "raise.h"
#include "exception.h"
#include "object.h"

extern struct PupClass StringClassInstance;

struct PupString {
	struct PupObject obj_header;
	char *value;
};

struct PupObject *pup_string_new_cstr(const char *str)
{
	struct PupString *string = malloc(sizeof(struct PupString));
	if (!malloc) {
		return NULL;
	}
	obj_init(&(string->obj_header), &StringClassInstance);
	string->value = strdup(str);
	return (struct PupObject *)string;
}

const char *pup_string_value_unsafe(struct PupObject *str)
{
	return ((struct PupString *)str)->value;
}

bool pup_is_string(struct PupObject *obj)
{
	return pup_object_instanceof(obj, &StringClassInstance);
}

const char *pup_string_value(struct PupObject *str)
{
	if (!pup_is_string(str)) {
		pup_raise(pup_new_runtimeerrorf("String expected, %s given",
		                                pup_object_type_name(str)));
	}
	return pup_string_value_unsafe(str);
}
