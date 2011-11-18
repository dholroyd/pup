#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "core_types.h"
#include "object.h"
#include "class.h"
#include "runtime.h"
#include "exception.h"
#include "string.h"
#include "abortf.h"

extern struct PupClass ExceptionClassInstance;

struct PupAttributeListEntry {
	long name_sym;
	struct PupObject *value;
	struct PupAttributeListEntry *next;
};


void obj_init(struct PupObject *obj, struct PupClass *type)
{
	obj->type = type;
	obj->attr_list_head = NULL;
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

const char *pup_object_type_name(const struct PupObject *obj)
{
	if (!obj) {
		return "<NULL Object ref>";
	}
	return pup_type_name(obj->type);
}

struct PupObject *pup_invoke(struct PupObject *target, const long name_sym,
                             const long argc, struct PupObject **argv)
{
	ABORTF_ON(!target, "no target for invocation of sym:%ld", name_sym);
	struct PupClass *class = target->type;
	PupMethod *method = find_method_in_classes(class, name_sym);
	if (!method) {
		char buf[256];
		snprintf(buf, sizeof(buf), "undefined method `sym:%ld' for %s", name_sym, pup_object_type_name(target));
		pup_raise_runtimeerror(buf);
	}
	return (*method)(target, argc, argv);
}

bool pup_object_instanceof(const struct PupObject *obj,
                          const struct PupClass *class)
{
	return obj->type == class;
}

bool pup_object_kindof(const struct PupObject *obj,
                       const struct PupClass *class)
{
	return pup_is_descendant_or_same(class, obj->type);
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

/*
 * Returns obj, if obj is a Class, or obj->type otherwise.
 * Used when we want to make use of 'self' without caring if we are in class
 * or instance context.
 */
struct PupClass *pup_class_context_from(struct PupObject *obj)
{
	if (pup_is_class_instance(obj)) {
		return (struct PupClass *)obj;
	}
	return obj->type;
}

static void pup_default_obj_cstr(const struct PupObject *obj,
                          char *buf,
                          const size_t buf_size)
{
	snprintf(buf, buf_size, "<%s:%p>",
	         pup_object_type_name(obj), obj);
}

METH_IMPL(pup_object_to_s)
{
	char buf[1024];
	pup_default_obj_cstr(target, buf, sizeof(buf));
	return pup_string_new_cstr(buf);
}

char *pup_stringify(struct PupObject *obj)
{
	if (pup_is_string(obj)) {
		return strdup(pup_string_value_unsafe(obj));
	}
	if (pup_object_instanceof(obj, &ExceptionClassInstance)) {
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
