
#include <stdlib.h>
#include "core_types.h"
#include "symtable.h"
#include "object.h"
#include "class.h"
#include "string.h"
#include "exception.h"
#include "heap.h"
#include "fixnum.h"
#include "abortf.h"
#include "stdio.h"

struct RuntimeEnv {
	struct PupHeap heap;
	struct SymTable *sym_tab;
	struct PupClass *class_object;
	struct PupClass *class_class;
	struct PupClass *class_string;
	struct PupClass *class_exception;
	struct PupClass *class_standarderror;
	struct PupClass *class_runtimeerror;
	struct PupClass *class_true;
	struct PupClass *class_false;
	struct PupObject *object_true;
	struct PupObject *object_false;
	struct PupClass *class_fixnum;
};


void pup_runtime_env_destroy(struct RuntimeEnv *env)
{
	if (env->sym_tab) {
		pup_sym_table_destroy(env->sym_tab);
	}
	/*
	pup_object_free(env->object_false);
	pup_object_free(env->object_true);
	pup_class_free(env->class_false);
	pup_class_free(env->class_true);
	pup_class_free(env->class_runtimeerror);
	pup_class_free(env->class_standarderror);
	pup_class_free(env->class_exception);
	pup_class_free(env->class_string);
	pup_class_free(env->class_class);
	pup_class_free(env->class_object);
	*/
	pup_heap_destroy(&env->heap);
	free(env);
}

static void runtime_init(struct RuntimeEnv *env)
{
	if (pup_heap_init(&env->heap)) {
		ABORTF("heap initialization failed");
	}
	env->class_object = pup_bootstrap_create_classobject(env);
	env->class_class = pup_bootstrap_create_classclass(env,
	                                                   env->class_object);  // super=Object
	obj_init((struct PupObject *)env->class_object, env->class_class);
	obj_init((struct PupObject *)env->class_class, env->class_class);
	pup_class_class_init(env, env->class_object);
	pup_object_class_init(env, env->class_object);

	pup_const_set(env, env->class_object,
	              pup_env_str_to_sym(env, "Object"),
	              (struct PupObject *)env->class_object);
	pup_const_set(env, env->class_object,
	              pup_env_str_to_sym(env, "Class"),
	              (struct PupObject *)env->class_class);
	env->class_string = pup_bootstrap_create_classstring(env);
	pup_const_set(env, env->class_object,
	              pup_env_str_to_sym(env, "String"),
	              (struct PupObject *)env->class_string);
	env->class_exception = pup_create_class(env,
	                                        env->class_object,
	                                        NULL,  // no lexical scope
	                                        "Exception");
	pup_const_set(env, env->class_object,
	              pup_env_str_to_sym(env, "Exception"),
	              (struct PupObject *)env->class_exception);
	env->class_standarderror = pup_create_class(env,
	                                            env->class_exception,
	                                            NULL,  // no lexical scope
	                                            "StandardError");
	pup_const_set(env, env->class_object,
	              pup_env_str_to_sym(env, "StandardError"),
	              (struct PupObject *)env->class_standarderror);
	env->class_runtimeerror = pup_create_class(env,
	                                           env->class_standarderror,
	                                           NULL,  // no lexical scope
	                                           "RuntimeError");
	pup_const_set(env, env->class_object,
	              pup_env_str_to_sym(env, "RuntimeError"),
	              (struct PupObject *)env->class_runtimeerror);
	env->class_true = pup_create_class(env,
	                                        env->class_object,
	                                        NULL,  // no lexical scope
	                                        "TrueClass");
	pup_const_set(env, env->class_object,
	              pup_env_str_to_sym(env, "TrueClass"),
	              (struct PupObject *)env->class_true);
	env->class_false = pup_create_class(env,
	                                        env->class_object,
	                                        NULL,  // no lexical scope
	                                        "FalseClass");
	pup_const_set(env, env->class_object,
	              pup_env_str_to_sym(env, "FalseClass"),
	              (struct PupObject *)env->class_false);
	env->object_true = pup_create_object(env, env->class_true);
	env->object_false = pup_create_object(env, env->class_false);

	pup_exception_class_init(env, env->class_exception);
	env->class_fixnum = pup_bootstrap_create_classfixnum(env);
	pup_const_set(env, env->class_object,
	              pup_env_str_to_sym(env, "Fixnum"),
	              (struct PupObject *)env->class_fixnum);
}

struct RuntimeEnv *pup_runtime_env_create()
{
	struct RuntimeEnv *env = malloc(sizeof(struct RuntimeEnv));
	if (!env) {
		return NULL;
	}
	env->sym_tab = pup_sym_table_create();
	if (!env->sym_tab) {
		goto error;
	}

	// TODO: defer and use exceptions for error handling?
	runtime_init(env);

	return env;
    error:
	pup_runtime_env_destroy(env);
	return NULL;
}

struct PupClass *pup_env_get_classobject(ENV)
{
	return env->class_object;
}

struct PupClass *pup_env_get_classclass(ENV)
{
	return env->class_class;
}

struct PupClass *pup_env_get_classstring(ENV)
{
	return env->class_string;
}

struct PupClass *pup_env_get_classexception(ENV)
{
	return env->class_exception;
}

struct PupClass *pup_env_get_classstandarderror(ENV)
{
	return env->class_standarderror;
}

struct PupClass *pup_env_get_classruntimeerror(ENV)
{
	return env->class_runtimeerror;
}

int pup_env_str_to_sym(ENV, char *str)
{
	return pup_str_to_sym(env->sym_tab, str);
}

const char *pup_env_sym_to_str(ENV, const int sym)
{
	return pup_sym_to_str(env->sym_tab, sym);
}

struct PupObject *pup_env_get_trueinstance(ENV)
{
	return env->object_true;
}

struct PupObject *pup_env_get_falseinstance(ENV)
{
	return env->object_false;
}

struct PupClass *pup_env_get_classfixnum(ENV)
{
	return env->class_fixnum;
}

void *pup_alloc_obj(ENV, size_t size)
{
	return pup_heap_alloc(&env->heap, size, PUP_KIND_OBJ);
}

void *pup_alloc_attr(ENV, size_t size)
{
	return pup_heap_alloc(&env->heap, size, PUP_KIND_ATTR);
}

void *pup_env_alloc_obj_for_gc_copy(ENV, size_t size)
{
	return pup_heap_alloc_for_gc_copy(&env->heap, size, PUP_KIND_OBJ);
}

void *pup_env_alloc_attr_for_gc_copy(ENV, size_t size)
{
	return pup_heap_alloc_for_gc_copy(&env->heap, size, PUP_KIND_ATTR);
}


struct MainArgs {
	struct RuntimeEnv *env;
	struct PupObject *main_obj;
	PupMethod *main_method;
};

static void *do_main(void *arg)
{
	struct MainArgs *args = (struct MainArgs *)arg;
	pup_heap_thread_init(&args->env->heap);
	struct PupObject *ret =
		(*args->main_method)(args->env, args->main_obj, 0, NULL);
	// TODO: pup_heap_thread_destroy(), or something named better
	return ret;
}

int pup_env_launch_main(ENV, PupMethod *main_method)
{
	struct PupClass *class_class = pup_env_get_classobject(env);
	struct PupObject *main_obj = pup_create_object(env, class_class);

	pthread_attr_t attr;
	if (pthread_attr_init(&attr)) {
		return 1;
	}

	struct MainArgs args = {
		.env = env,
		.main_obj = main_obj,
		.main_method = main_method
	};
	pthread_t main_thread;
	int res = pthread_create(&main_thread,
	                         &attr,
	                         do_main,
	                         (void *)&args);
	// FIXME: proper error handling
	ABORTF_ON(res, "pthread_create() returned %d", res);
	void *retval;
	res = pthread_join(main_thread, &retval);
	// FIXME: proper error handling
	ABORTF_ON(res, "pthread_join() returned %d", res);
	return 0;
}

void pup_env_safepoint(ENV)
{
	pup_heap_safepoint(&env->heap);
}
