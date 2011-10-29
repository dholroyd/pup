#include <stdlib.h>
#include <string.h>
#include <stdio.h>
//#include <libunwind.h>
#include "abortf.h"
#include "core_types.h"
#include "runtime.h"
#include "raise.h"

extern struct Class ExceptionClassInstance;
extern struct Class StringClassInstance;

/*
static void dump_stack()
{
	unw_context_t context;
	unw_cursor_t cursor;
	if (unw_getcontext(&context)) {
		return;
	}
	if (unw_init_local(&cursor, &context)) {
		return;
	}
	do {
		char name[70];
		unw_word_t off;
		if (unw_get_proc_name(&cursor, name, 70, &off)) {
			fprintf(stderr, "<func name unavailable>()\n");
		} else {
			fprintf(stderr, "%s()\n", name);
		}
	} while (unw_step(&cursor) > 0);
}
*/

struct Exception *pup_new_runtimeerror(const char *message)
{
	struct Exception *e
		= (struct Exception *)malloc(sizeof(struct Exception));
	obj_init(&(e->obj_header), &ExceptionClassInstance);
	e->message = strdup(message);
	return e;
}

struct Exception *pup_new_runtimeerrorf(const char *messagefmt, ...)
{
	va_list ap;
	struct Exception *e
		= (struct Exception *)malloc(sizeof(struct Exception));
	obj_init(&(e->obj_header), &ExceptionClassInstance);
	e->message = malloc(1024);
	va_start(ap, messagefmt);
	vsnprintf(e->message, 1024, messagefmt, ap);
	va_end(ap);
	return e;
}

void pup_raise_runtimeerror(const char *message)
{
	pup_raise(pup_new_runtimeerror(message));
}

void pup_handle_uncaught_exception(const struct _Unwind_Exception *e)
{
	struct Exception *ex = extract_exception_obj(e);
	fprintf(stderr, "Uncaught %s: \"%s\"\n",
	       pup_type_name_of((struct Object *)ex), ex->message);
	exit(1);
}

METH_IMPL(pup_exception_to_s)
{
	return pup_string_new_cstr(((struct Exception *)target)->message);
}

// TODO move to Kernel class
METH_IMPL(pup_object_raise)
{
	pup_arity_check(1, argc);
	struct Object *arg = argv[0];
	if (pup_is_class(arg, &ExceptionClassInstance)) {
		pup_raise((struct Exception *)arg);
	}
	if (pup_is_class(arg, &StringClassInstance)) {
		pup_raise_runtimeerror(((struct String *)arg)->value);
	}
	fprintf(stderr, "%s\n", arg->type->name);
	// TODO exception classes
	pup_raise_runtimeerror("exception class/object expected");
	abort();
}

