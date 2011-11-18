
#include <stdlib.h>
#include <string.h>
#include "core_types.h"
#include "runtime.h"

extern struct PupClass StringClassInstance;

struct PupString *pup_string_create(const char *data)
{
	struct PupString *result = malloc(sizeof(struct PupString));
	obj_init(&(result->obj_header), &StringClassInstance);
	result->value = strdup(data);
	return result;
}
