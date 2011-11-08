#include "core_types.h"
#include "runtime.h"


METH_IMPL(pup_class_to_s)
{
	return pup_string_new_cstr(((struct Class *)target)->name);
}
