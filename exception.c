#include <stdlib.h>
#include <string.h>
#include <stdio.h>
//#include <libunwind.h>
#include "abortf.h"
#include "core_types.h"
#include "runtime.h"
#include "raise.h"

extern struct Class ExceptionClassInstance;
extern struct Class RuntimeErrorClassInstance;
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

extern void pup_exception_message_set(struct Object *target,
                                      struct Object *value);


struct Object *pup_new_runtimeerror(const char *message)
{
	ABORT_ON(!message, "message must not be null");
	struct Object *e = pup_create_object(&RuntimeErrorClassInstance);
	pup_exception_message_set(e, pup_string_new_cstr(message));
	return e;
}

struct Object *pup_new_runtimeerrorf(const char *messagefmt, ...)
{
	va_list ap;
	struct Object *e = pup_create_object(&RuntimeErrorClassInstance);
	char message[1024];
	va_start(ap, messagefmt);
	vsnprintf(message, sizeof(message), messagefmt, ap);
	va_end(ap);
	pup_exception_message_set(e, pup_string_new_cstr(message));
	return e;
}

void pup_raise_runtimeerror(const char *message)
{
	pup_raise(pup_new_runtimeerror(message));
}

extern struct Object *pup_exception_message_get(struct Object *target);

const char *exception_text(struct Object *ex)
{
	struct String *msg = (struct String *)pup_exception_message_get(ex);
	if (msg) {
		return msg->value;
	}
	return pup_type_name_of((struct Object *)ex);
}

void pup_handle_uncaught_exception(const struct _Unwind_Exception *e)
{
	struct Object *ex = extract_exception_obj(e);
	fprintf(stderr, "Uncaught %s: \"%s\"\n",
	       pup_type_name_of((struct Object *)ex), exception_text(ex));
	exit(1);
}

METH_IMPL(pup_exception_to_s)
{
	char buf[1024];
	snprintf(buf, sizeof(buf), "#<%s: %s>",
	         pup_type_name_of(target),
	         exception_text(((struct Object *)target)));
	return pup_string_new_cstr(buf);
}

METH_IMPL(pup_exception_message)
{
	return pup_exception_message_get(target);
}

METH_IMPL(pup_exception_initialize)
{
	if (argc == 1) {
		pup_exception_message_set(target, argv[0]);
	} else if (argc > 1) {
		pup_arity_check(1, argc);
	}
	return NULL;
}

// TODO move to Kernel class
METH_IMPL(pup_object_raise)
{
	pup_arity_check(1, argc);
	struct Object *arg = argv[0];
	if (pup_is_descendant_or_same(&ExceptionClassInstance, arg->type)) {
		pup_raise((struct Object *)arg);
		abort();
	}
	if (pup_is_class(arg, &StringClassInstance)) {
		pup_raise_runtimeerror(((struct String *)arg)->value);
		abort();
	}
	// TODO exception classes
	pup_raise_runtimeerror("exception class/object expected");
	abort();
}

