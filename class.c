#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "core_types.h"
#include "runtime.h"
#include "string.h"
#include "raise.h"
#include "exception.h"
#include "object.h"
#include "abortf.h"

struct MethodListEntry {
	long name_sym;
	PupMethod *method;
	struct MethodListEntry *next;
};

struct PupClass {
	struct PupObject obj_header;
	struct PupClass *superclass;
	char *name;
	struct MethodListEntry *method_list_head;
	struct PupClass *scope;  /* for Constant lookup */
};

struct PupClass *pup_create_class(ENV,
                                  struct PupClass *class_class,
                                  struct PupClass *superclass,
                                  struct PupClass *scope,
			          const char *name)
{
	struct PupClass *class = (struct PupClass *)malloc(sizeof(struct PupClass));
	obj_init(&class->obj_header, class_class);
	class->name = strdup(name);
	class->superclass = superclass;
	class->method_list_head = NULL;
	class->scope = scope;
	return class;
}


/*
 * Adds the given PupMethod to the method table of the given PupClass
 */
void pup_define_method(struct PupClass *class, const long name_sym, PupMethod *method)
{
	struct MethodListEntry **pos;
	struct MethodListEntry *new;

	ABORT_ON(!class,
		"Class reference given to pup_define_method() must not be null");
	pos = &class->method_list_head;
	while (*pos) {
		pos = &(*pos)->next;
	}
	new = (struct MethodListEntry *)malloc(sizeof(struct MethodListEntry));
	new->name_sym = name_sym;
	new->method = method;
	new->next = NULL;
	*pos = new;
}

const char *pup_type_name(const struct PupClass *type)
{
	if (!type) {
		return "<Object with NULL type ref!>";
	}
	if (!type->name) {
		return "<NULL type name!>";
	}
	return type->name;
}

static PupMethod *find_method_in_list(struct MethodListEntry *meth_list,
                                   const long name_sym)
{
	while (meth_list) {
		if (meth_list->name_sym == name_sym) {
			return meth_list->method;
		}
		meth_list = meth_list->next;
	}
	return NULL;
}

PupMethod *find_method_in_classes(struct PupClass *class,
                                         const long name_sym)
{
	while (class) {
		PupMethod *method = find_method_in_list(class->method_list_head,
		                                     name_sym);
		if (method) {
			return method;
		}
		class = class->superclass;
	}
	return NULL;
}

bool pup_is_descendant_or_same(const struct PupClass *ancestor,
                              const struct PupClass *descendant)
{
	ABORT_ON(!ancestor, "ancestor must not be NULL");
	ABORT_ON(!descendant, "descendant must not be NULL");
	for (const struct PupClass *next = descendant; next; next=next->superclass) {
		if (next == ancestor) {
			return true;
		}
	}
	return false;
}

void pup_const_set(struct PupClass* clazz, const int sym, struct PupObject *val)
{
	pup_iv_set(&clazz->obj_header, sym, val);
}

struct PupObject *pup_const_get(struct PupClass *clazz, const int sym)
{
	struct PupObject *result;
	struct PupClass *lookup = clazz;
	/* TODO: while nil, rather than while NULL */
	while ((result = pup_iv_get(&lookup->obj_header, sym)) == NULL) {
		lookup = lookup->scope;
		if (!lookup) break;
	}
	return result;
}

struct PupObject *pup_const_get_required(ENV, struct PupClass *clazz, const int sym)
{
	struct PupObject *res = pup_const_get(clazz, sym);
	if (!res) {
		// TODO: NameError
		pup_raise(pup_new_runtimeerrorf(env, "uninitialized constant %s",
		                                pup_env_sym_to_str(env, sym)));
	}
	return res;
}

bool pup_is_class_instance(ENV, const struct PupObject *obj)
{
	return obj->type == pup_env_get_classclass(env);
}

METH_IMPL(pup_class_to_s)
{
	return pup_string_new_cstr(env, ((struct PupClass *)target)->name);
}

void pup_class_free(struct PupClass *class)
{
	struct MethodListEntry *pos = class->method_list_head;
	while (pos) {
		struct MethodListEntry *tmp = pos;
		pos = pos->next;
		free(tmp);
	}
	free(class->name);
	pup_object_destroy((struct PupObject *)class);
	free(class);
}

METH_IMPL(pup_class_new)
{
	struct PupObject *res =
		pup_invoke(env, target, pup_env_str_to_sym(env, "allocate"),
		           0 , NULL);
	pup_invoke(env, res, pup_env_str_to_sym(env, "initialize"),
	           argc , argv);
	return res;
}

void pup_class_class_init(ENV, struct PupClass *class_class)
{
	pup_define_method(class_class,
	                  pup_env_str_to_sym(env, "new"),
	                  pup_class_new);
	pup_define_method(class_class,
	                  pup_env_str_to_sym(env, "allocate"),
	                  pup_object_allocate);
	pup_define_method(class_class,
	                  pup_env_str_to_sym(env, "to_s"),
	                  pup_class_to_s);
}
