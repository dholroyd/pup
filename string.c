
#include <stdlib.h>
#include <string.h>
#include "core_types.h"
#include "runtime.h"

extern struct Class StringClassInstance;

struct String *pup_string_create(const char *data)
{
	struct String *result = malloc(sizeof(struct String));
	obj_init(&(result->obj_header), &StringClassInstance);
	result->value = strdup(data);
	return result;
}
