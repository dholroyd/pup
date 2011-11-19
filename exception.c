#include <stdlib.h>
#include <string.h>
#include <stdio.h>
//#include <libunwind.h>
#include "abortf.h"
#include "core_types.h"
#include "runtime.h"
#include "raise.h"
#include "string.h"
#include "object.h"

extern struct PupClass ExceptionClassInstance;
extern struct PupClass RuntimeErrorClassInstance;

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

extern void pup_exception_message_set(struct PupObject *target,
                                      struct PupObject *value);

bool pup_instanceof_exception(struct PupObject *obj)
{
	return pup_object_instanceof(obj, &ExceptionClassInstance);
}

struct PupObject *pup_new_runtimeerror(ENV, const char *message)
{
	ABORT_ON(!message, "message must not be null");
	struct PupObject *e = pup_create_object(env, &RuntimeErrorClassInstance);
	pup_exception_message_set(e, pup_string_new_cstr(message));
	return e;
}

struct PupObject *pup_new_runtimeerrorf(ENV, const char *messagefmt, ...)
{
	va_list ap;
	struct PupObject *e = pup_create_object(env, &RuntimeErrorClassInstance);
	char message[1024];
	va_start(ap, messagefmt);
	vsnprintf(message, sizeof(message), messagefmt, ap);
	va_end(ap);
	pup_exception_message_set(e, pup_string_new_cstr(message));
	return e;
}

void pup_raise_runtimeerror(ENV, const char *message)
{
	pup_raise(pup_new_runtimeerror(env, message));
}

extern struct PupObject *pup_exception_message_get(struct PupObject *target);

const char *exception_text(struct PupObject *ex)
{
	struct PupObject *msg = pup_exception_message_get(ex);
	if (msg) {
		return pup_string_value(msg);
	}
	return pup_object_type_name(ex);
}

void pup_handle_uncaught_exception(const struct _Unwind_Exception *e)
{
	struct PupObject *ex = extract_exception_obj(e);
	fprintf(stderr, "Uncaught %s: \"%s\"\n",
	       pup_object_type_name((struct PupObject *)ex), exception_text(ex));
	exit(1);
}

METH_IMPL(pup_exception_to_s)
{
	char buf[1024];
	snprintf(buf, sizeof(buf), "#<%s: %s>",
	         pup_object_type_name(target),
	         exception_text(((struct PupObject *)target)));
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
	struct PupObject *arg = argv[0];
	if (pup_object_kindof(arg, &ExceptionClassInstance)) {
		pup_raise(arg);
		abort();
	}
	if (pup_is_string(arg)) {
		pup_raise_runtimeerror(env, pup_string_value_unsafe(arg));
		abort();
	}
	// TODO exception classes
	pup_raise_runtimeerror(env, "exception class/object expected");
	abort();
}

