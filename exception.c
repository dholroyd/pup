#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
//#include <libunwind.h>
#include "abortf.h"
#include "core_types.h"
#include "runtime.h"
#include "class.h"
#include "raise.h"
#include "string.h"
#include "object.h"

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

void pup_exception_message_set(ENV,
                               struct PupObject *target,
                               struct PupObject *value)
{
	const int sym_message = pup_env_str_to_sym(env, "@message");
	pup_iv_set(env, target, sym_message, value);
}

bool pup_instanceof_exception(ENV, struct PupObject *obj)
{
	return pup_object_instanceof(obj, pup_env_get_classexception(env));
}

struct PupObject *pup_new_runtimeerror(ENV, const char *message)
{
	ABORT_ON(!message, "message must not be null");
	struct PupObject *e = pup_create_object(env, pup_env_get_classruntimeerror(env));
	pup_exception_message_set(env, e, pup_string_new_cstr(env, message));
	return e;
}

struct PupObject *pup_new_runtimeerrorf(ENV, const char *messagefmt, ...)
{
	va_list ap;
	struct PupObject *e = pup_create_object(env, pup_env_get_classruntimeerror(env));
	char message[1024];
	va_start(ap, messagefmt);
	vsnprintf(message, sizeof(message), messagefmt, ap);
	va_end(ap);
	pup_exception_message_set(env, e, pup_string_new_cstr(env, message));
	return e;
}

void pup_raise_runtimeerror(ENV, const char *message)
{
	pup_raise(pup_new_runtimeerror(env, message));
}

struct PupObject *pup_exception_message_get(ENV, struct PupObject *target)
{
	const int sym_message = pup_env_str_to_sym(env, "@message");
	return pup_iv_get(target, sym_message);
}

const char *exception_text(ENV, struct PupObject *ex)
{
	struct PupObject *msg = pup_exception_message_get(env, ex);
	if (msg) {
		return pup_string_value(env, msg);
	}
	return pup_object_type_name(ex);
}

void pup_handle_uncaught_exception(ENV, const struct _Unwind_Exception *e)
{
	struct PupObject *ex = extract_exception_obj(e);
	fprintf(stderr, "Uncaught %s: \"%s\"\n",
	       pup_object_type_name((struct PupObject *)ex), exception_text(env, ex));
	exit(1);
}

METH_IMPL(pup_exception_to_s)
{
	char buf[1024];
	snprintf(buf, sizeof(buf), "#<%s: %s>",
	         pup_object_type_name(target),
	         exception_text(env, ((struct PupObject *)target)));
	return pup_string_new_cstr(env, buf);
}

METH_IMPL(pup_exception_message)
{
	return pup_exception_message_get(env, target);
}

METH_IMPL(pup_exception_initialize)
{
	if (argc == 1) {
		pup_exception_message_set(env, target, argv[0]);
	} else if (argc > 1) {
		pup_arity_check(env, 1, argc);
	}
	return NULL;
}

// TODO move to Kernel class
METH_IMPL(pup_object_raise)
{
	pup_arity_check(env, 1, argc);
	struct PupObject *arg = argv[0];
	if (pup_object_kindof(arg, pup_env_get_classexception(env))) {
		pup_raise(arg);
		abort();
	}
	if (pup_is_string(env, arg)) {
		pup_raise_runtimeerror(env, pup_string_value_unsafe(arg));
		abort();
	}
	// TODO exception classes
	pup_raise_runtimeerror(env, "exception class/object expected");
	abort();
}

void pup_exception_class_init(ENV, struct PupClass *class_excep)
{
	pup_define_method(class_excep,
	                  pup_env_str_to_sym(env, "initialize"),
	                  pup_exception_initialize);
	pup_define_method(class_excep,
	                  pup_env_str_to_sym(env, "message"),
	                  pup_exception_message);
}
