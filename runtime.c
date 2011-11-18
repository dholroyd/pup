
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "core_types.h"
#include "runtime.h"
#include "abortf.h"
#include "raise.h"
#include "exception.h"
#include "string.h"

extern struct PupClass ClassClassInstance;
extern struct PupClass ExceptionClassInstance;

void obj_init(struct PupObject *obj, struct PupClass *type)
{
	obj->type = type;
	obj->attr_list_head = NULL;
}

struct PupClass *pup_create_class(struct PupClass *class_class,
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
	struct PupMethodListEntry **pos;
	struct PupMethodListEntry *new;

	ABORT_ON(!class,
		"Class reference given to pup_define_method() must not be null");
	pos = &class->method_list_head;
	while (*pos) {
		pos = &(*pos)->next;
	}
	new = (struct PupMethodListEntry *)malloc(sizeof(struct PupMethodListEntry));
	new->name_sym = name_sym;
	new->method = method;
	new->next = NULL;
	*pos = new;
}

METH_IMPL(pup_object_allocate)
{
	struct PupObject *obj = (struct PupObject *)malloc(sizeof(struct PupObject));
	obj_init(obj, (struct PupClass *)target);
	return obj;
}

struct PupObject *pup_create_object(struct PupClass *type)
{
	return pup_object_allocate((struct PupObject *)type, 0, NULL);
}

/*
 * Default implementation for Object#initialize
 */
METH_IMPL(pup_object_initialize)
{
	// TODO: return nil
	return NULL;
}

const char *pup_type_name_of(const struct PupObject *obj)
{
	if (!obj) {
		return "<NULL Object ref>";
	}
	if (!obj->type) {
		return "<Object with NULL type ref!>";
	}
	if (!obj->type->name) {
		return "<NULL type name!>";
	}
	return obj->type->name;
}

void pup_default_obj_cstr(const struct PupObject *obj,
                          char *buf,
                          const size_t buf_size)
{
	snprintf(buf, buf_size, "<%s:%p>",
	         pup_type_name_of(obj), obj);
}

void pup_arity_check(int expected, int actual)
{
	if (expected != actual) {
		// TODO: ArgumentError
		pup_raise(pup_new_runtimeerrorf("wrong number of arguments (%d for %d)",
		                        actual, expected));
	}
}

METH_IMPL(pup_object_to_s)
{
	char buf[1024];
	pup_default_obj_cstr(target, buf, sizeof(buf));
	return pup_string_new_cstr(buf);
}

static PupMethod *find_method_in_list(struct PupMethodListEntry *meth_list,
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

static PupMethod *find_method_in_classes(struct PupClass *class, const long name_sym)
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

struct PupObject *pup_invoke(struct PupObject *target, const long name_sym,
                             const long argc, struct PupObject **argv)
{
	ABORTF_ON(!target, "no target for invocation of sym:%ld", name_sym);
	struct PupClass *class = target->type;
	PupMethod *method = find_method_in_classes(class, name_sym);
	if (!method) {
		char buf[256];
		snprintf(buf, sizeof(buf), "undefined method `sym:%ld' for %s", name_sym, pup_type_name_of(target));
		pup_raise_runtimeerror(buf);
	}
	return (*method)(target, argc, argv);
}

int pup_is_class(const struct PupObject *obj, const struct PupClass *class)
{
	return obj->type == class;
}

int pup_is_descendant_or_same(const struct PupClass *ancestor,
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

char *pup_stringify(struct PupObject *obj)
{
	if (pup_is_string(obj)) {
		return strdup(pup_string_value_unsafe(obj));
	}
	if (pup_is_class(obj, &ExceptionClassInstance)) {
		const char *msg = exception_text(obj);
		if (msg) {
			return strdup(msg);
		}
	}
	char buf[1024];
	pup_default_obj_cstr(obj, buf, sizeof(buf));
	return strdup(buf);
}

METH_IMPL(pup_puts)
{
	pup_arity_check(1, argc);
	ABORT_ON(!argv, "puts() argv is NULL!");
	char *str = pup_stringify(argv[0]);
	puts(str);
	free(str);

	// TODO return nil
	return NULL;
}

static struct PupAttributeListEntry *find_attr(const struct PupObject *obj,
                                     const int sym)
{
	struct PupAttributeListEntry *attr = obj->attr_list_head;
	while (attr) {
		if (attr->name_sym == sym) {
			return attr;
		}
		attr = attr->next;
	}
	return NULL;
}

static struct PupAttributeListEntry *create_attr(const int sym,
                                              struct PupObject *val,
					      struct PupAttributeListEntry *next)
{
	struct PupAttributeListEntry *attr
		= malloc(sizeof(struct PupAttributeListEntry));
	attr->name_sym = sym;
	attr->value = val;
	attr->next = next;
	return attr;
}

void pup_iv_set(struct PupObject *obj, const int sym, struct PupObject *val)
{
	struct PupAttributeListEntry *attr = find_attr(obj, sym);
	// TODO: what to do about these race conditions?
	if (attr) {
		attr->value = val;
	} else {
		obj->attr_list_head= create_attr(sym, val, obj->attr_list_head);
	}
}

struct PupObject *pup_iv_get(struct PupObject *obj, const int sym)
{
	struct PupAttributeListEntry *attr = find_attr(obj, sym);
	if (attr) {
		return attr->value;
	}
	return NULL;  /* TODO nil */
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

struct PupObject *pup_const_get_required(struct PupClass *clazz, const int sym)
{
	struct PupObject *res = pup_const_get(clazz, sym);
	if (!res) {
		// TODO: NameError
		pup_raise(pup_new_runtimeerrorf("uninitialized constant sym:%ld",
		                                sym));
	}
	return res;
}

/*
 * Returns obj, if obj is a Class, or obj->type otherwise.
 * Used when we want to make use of 'self' without caring if we are in class
 * or instance context.
 */
struct PupClass *pup_class_context_from(struct PupObject *obj)
{
	if (obj->type == &ClassClassInstance) {
		return (struct PupClass *)obj;
	}
	return obj->type;
}
