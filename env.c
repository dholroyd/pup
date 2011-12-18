
#include <stdlib.h>
#include "core_types.h"
#include "symtable.h"
#include "object.h"
#include "class.h"
#include "exception.h"

struct RuntimeEnv {
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
};


void pup_runtime_env_destroy(struct RuntimeEnv *env)
{
	if (env->sym_tab) {
		pup_sym_table_destroy(env->sym_tab);
	}
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
	free(env);
}

static void runtime_init(struct RuntimeEnv *env)
{
	env->class_object = pup_create_class(env, NULL,  // no class 'Class' yet!
	                                     NULL,  // no superclass for Object
	                                     NULL,  // no lexical scope
	                                     "Object");
	env->class_class = pup_create_class(env, NULL,  // no class 'Class' yet
	                                    env->class_object,  // super=Object
	                                    NULL,  // no lexical scope
	                                    "Class");
	obj_init((struct PupObject *)env->class_object, env->class_class);
	obj_init((struct PupObject *)env->class_class, env->class_class);
	pup_const_set(env->class_object,
	              pup_env_str_to_sym(env, "Object"),
	              (struct PupObject *)env->class_object);
	pup_const_set(env->class_object,
	              pup_env_str_to_sym(env, "Class"),
	              (struct PupObject *)env->class_class);
	env->class_string = pup_create_class(env, env->class_class,
	                                        env->class_object,
	                                        NULL,  // no lexical scope
	                                        "String");
	pup_const_set(env->class_object,
	              pup_env_str_to_sym(env, "String"),
	              (struct PupObject *)env->class_string);
	env->class_exception = pup_create_class(env, env->class_class,
	                                        env->class_object,
	                                        NULL,  // no lexical scope
	                                        "Exception");
	pup_const_set(env->class_object,
	              pup_env_str_to_sym(env, "Exception"),
	              (struct PupObject *)env->class_exception);
	env->class_standarderror = pup_create_class(env, env->class_class,
	                                            env->class_exception,
	                                            NULL,  // no lexical scope
	                                            "StandardError");
	pup_const_set(env->class_object,
	              pup_env_str_to_sym(env, "StandardError"),
	              (struct PupObject *)env->class_standarderror);
	env->class_runtimeerror = pup_create_class(env, env->class_class,
	                                           env->class_standarderror,
	                                           NULL,  // no lexical scope
	                                           "RuntimeError");
	pup_const_set(env->class_object,
	              pup_env_str_to_sym(env, "RuntimeError"),
	              (struct PupObject *)env->class_runtimeerror);
	env->class_true = pup_create_class(env, env->class_class,
	                                        env->class_object,
	                                        NULL,  // no lexical scope
	                                        "TrueClass");
	pup_const_set(env->class_object,
	              pup_env_str_to_sym(env, "TrueClass"),
	              (struct PupObject *)env->class_true);
	env->class_false = pup_create_class(env, env->class_class,
	                                        env->class_object,
	                                        NULL,  // no lexical scope
	                                        "FalseClass");
	pup_const_set(env->class_object,
	              pup_env_str_to_sym(env, "FalseClass"),
	              (struct PupObject *)env->class_false);
	env->object_true = pup_create_object(env, env->class_true);
	env->object_false = pup_create_object(env, env->class_false);

	pup_class_class_init(env, env->class_object);
	pup_object_class_init(env, env->class_object);
	pup_exception_class_init(env, env->class_exception);
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
