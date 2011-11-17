
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "core_types.h"
#include "runtime.h"
#include "abortf.h"
#include "raise.h"
#include "exception.h"

extern struct Class StringClassInstance;
extern struct Class ClassClassInstance;
extern struct Class ExceptionClassInstance;

void obj_init(struct Object *obj, struct Class *type)
{
	obj->type = type;
	obj->attr_list_head = NULL;
}

struct Class *pup_create_class(struct Class *class_class,
                               struct Class *superclass,
                               struct Class *scope,
			       const char *name)
{
	struct Class *class = (struct Class *)malloc(sizeof(struct Class));
	obj_init(&class->obj_header, class_class);
	class->name = strdup(name);
	class->superclass = superclass;
	class->method_list_head = NULL;
	class->scope = scope;
	return class;
}

/*
 * Adds the given Method to the method table of the given Class
 */
void pup_define_method(struct Class *class, const long name_sym, Method *method)
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

METH_IMPL(pup_object_allocate)
{
	struct Object *obj = (struct Object *)malloc(sizeof(struct Object));
	obj_init(obj, (struct Class *)target);
	return obj;
}

struct Object *pup_create_object(struct Class *type)
{
	return pup_object_allocate((struct Object *)type, 0, NULL);
}

/*
 * Default implementation for Object#initialize
 */
METH_IMPL(pup_object_initialize)
{
	// TODO: return nil
	return NULL;
}

const char *pup_type_name_of(const struct Object *obj)
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

struct Object *pup_string_new_cstr(const char *str)
{
	struct String *string = malloc(sizeof(struct String));
	if (!malloc) {
		return NULL;
	}
	obj_init(&(string->obj_header), &StringClassInstance);
	string->value = strdup(str);
	return (struct Object *)string;
}

void pup_default_obj_cstr(const struct Object *obj,
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

static Method *find_method_in_list(struct MethodListEntry *meth_list,
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

static Method *find_method_in_classes(struct Class *class, const long name_sym)
{
	while (class) {
		Method *method = find_method_in_list(class->method_list_head,
		                                     name_sym);
		if (method) {
			return method;
		}
		class = class->superclass;
	}
	return NULL;
}

struct Object *pup_invoke(struct Object *target, const long name_sym,
                          const long argc, struct Object **argv)
{
	ABORTF_ON(!target, "no target for invocation of sym:%ld", name_sym);
	struct Class *class = target->type;
	Method *method = find_method_in_classes(class, name_sym);
	if (!method) {
		char buf[256];
		snprintf(buf, sizeof(buf), "undefined method `sym:%ld' for %s", name_sym, pup_type_name_of(target));
		pup_raise_runtimeerror(buf);
	}
	return (*method)(target, argc, argv);
}

int pup_is_class(const struct Object *obj, const struct Class *class)
{
	return obj->type == class;
}

int pup_is_descendant_or_same(const struct Class *ancestor,
                              const struct Class *descendant)
{
	ABORT_ON(!ancestor, "ancestor must not be NULL");
	ABORT_ON(!descendant, "descendant must not be NULL");
	for (const struct Class *next = descendant; next; next=next->superclass) {
		if (next == ancestor) {
			return true;
		}
	}
	return false;
}

char *pup_stringify(struct Object *obj)
{
	if (pup_is_class(obj, &StringClassInstance)) {
		return strdup(((struct String *)obj)->value);
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

static struct AttributeListEntry *find_attr(const struct Object *obj,
                                     const int sym)
{
	struct AttributeListEntry *attr = obj->attr_list_head;
	while (attr) {
		if (attr->name_sym == sym) {
			return attr;
		}
		attr = attr->next;
	}
	return NULL;
}

static struct AttributeListEntry *create_attr(const int sym,
                                              struct Object *val,
					      struct AttributeListEntry *next)
{
	struct AttributeListEntry *attr
		= malloc(sizeof(struct AttributeListEntry));
	attr->name_sym = sym;
	attr->value = val;
	attr->next = next;
	return attr;
}

void pup_iv_set(struct Object *obj, const int sym, struct Object *val)
{
	struct AttributeListEntry *attr = find_attr(obj, sym);
	// TODO: what to do about these race conditions?
	if (attr) {
		attr->value = val;
	} else {
		obj->attr_list_head= create_attr(sym, val, obj->attr_list_head);
	}
}

struct Object *pup_iv_get(struct Object *obj, const int sym)
{
	struct AttributeListEntry *attr = find_attr(obj, sym);
	if (attr) {
		return attr->value;
	}
	return NULL;  /* TODO nil */
}

void pup_const_set(struct Class* clazz, const int sym, struct Object *val)
{
	pup_iv_set(&clazz->obj_header, sym, val);
}

struct Object *pup_const_get(struct Class *clazz, const int sym)
{
	struct Object *result;
	struct Class *lookup = clazz;
	/* TODO: while nil, rather than while NULL */
	while ((result = pup_iv_get(&lookup->obj_header, sym)) == NULL) {
		lookup = lookup->scope;
		if (!lookup) break;
	}
	return result;
}

struct Object *pup_const_get_required(struct Class *clazz, const int sym)
{
	struct Object *res = pup_const_get(clazz, sym);
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
struct Class *pup_class_context_from(struct Object *obj)
{
	if (obj->type == &ClassClassInstance) {
		return (struct Class *)obj;
	}
	return obj->type;
}
