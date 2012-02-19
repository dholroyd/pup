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
	struct PupObject *(*allocate_instance)(ENV, struct PupClass *);  /* hax: until we have instance methods */
};

void pup_internal_class_init(ENV,
                             struct PupClass *class,
                             struct PupClass *superclass,
                             struct PupClass *scope,
                             const char *name,
                             struct PupObject *(*allocate_instance)(ENV, struct PupClass *))
{
	ABORTF_ON(!name, "'name' must not ne null");
	ABORTF_ON(!allocate_instance, "'allocate_instance' must not ne null");
	class->name = strdup(name);
	class->superclass = superclass;
	class->method_list_head = NULL;
	class->scope = scope;
	class->allocate_instance = allocate_instance;
}

struct PupClass *pup_internal_create_class(ENV,
                                           struct PupClass *superclass,
                                           struct PupClass *scope,
                                           const char *name,
                                           struct PupObject *(*allocate_instance)(ENV, struct PupClass *))
{
	ABORTF_ON(!superclass, "'superclass' must not be NULL when creating class %s", name);
	ABORTF_ON(!allocate_instance, "'allocate_instance' must not be NULL when creating class %s", name);
	struct PupClass *class_class = pup_env_get_classclass(env);
	struct PupObject *o = pup_invoke(env, (struct PupObject *)class_class,
	                                 pup_env_str_to_sym(env, "new"),
	                                 0, NULL);
	struct PupClass *class = (struct PupClass *)o;
	pup_internal_class_init(env, class, superclass, scope, name, allocate_instance);
	return class;
}

struct PupClass *pup_create_class(ENV,
                                  struct PupClass *superclass,
                                  struct PupClass *scope,
                                  const char *name)
{
	return pup_internal_create_class(env,
	                                 superclass,
	                                 scope,
	                                 name,
	                                 &pup_object_allocate_instance);
}

// TODO: a name better differentiated from pup_class_allocate_instance()
struct PupObject *pup_internal_class_allocate_instance(
	ENV,
	struct PupClass *type
) {
	struct PupObject *obj =
		(struct PupObject *)pup_alloc(env, sizeof(struct PupClass));
	obj_init(obj, type);
	//struct PupClass *clazz = (struct PupClass *)obj;
	// TODO: NULL any fields?
	return obj;
}



struct PupClass *pup_bootstrap_create_classclass(ENV, struct PupClass *class_object)
{
	struct PupClass *class =
		(struct PupClass *)pup_internal_class_allocate_instance(env, NULL);
	pup_internal_class_init(env, class, class_object, class_object, "Class",
	                        &pup_internal_class_allocate_instance);
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

void pup_const_set(ENV, struct PupClass* clazz,
                   const int sym, struct PupObject *val)
{
	pup_iv_set(env, &clazz->obj_header, sym, val);
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

struct PupObject *pup_class_allocate_instance(ENV, struct PupClass *clazz)
{
	ABORTF_ON(!clazz, "clazz must not be null");
	ABORTF_ON(!clazz->allocate_instance, "clazz->allocate_instance must not be null for class %p", clazz);
	return clazz->allocate_instance(env, clazz);
}

METH_IMPL(pup_class_to_s)
{
	return pup_string_new_cstr(env, ((struct PupClass *)target)->name);
}
/*
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
*/

METH_IMPL(pup_class_new)
{
	struct PupObject *res =
		pup_invoke(env, target, pup_env_str_to_sym(env, "allocate"),
		           0 , NULL);
	// return value ignored,
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
