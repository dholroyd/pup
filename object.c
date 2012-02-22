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
#include "heap.h"

struct PupAttributeListEntry {
	long name_sym;
	struct PupObject *value;
	struct PupAttributeListEntry *next;
};

struct PupClass *pup_bootstrap_create_classobject(ENV)
{
	struct PupClass *class =
		(struct PupClass *)pup_internal_class_allocate_instance(env, NULL);
	pup_internal_class_init(env, class, NULL, NULL, "Object",
	                        &pup_object_allocate_instance,
	                        &pup_object_destroy_instance);
	return class;
}

void obj_init(struct PupObject *obj, struct PupClass *type)
{
	obj->type = type;
	obj->attr_list_head = NULL;
}

METH_IMPL(pup_object_allocate)
{
	return pup_class_allocate_instance(env, (struct PupClass *)target);
}

struct PupObject *pup_object_allocate_instance(ENV, struct PupClass *type)
{
	struct PupObject *obj = (struct PupObject *)pup_alloc_obj(env, sizeof(struct PupObject));
	obj_init(obj, type);
	return obj;
}

void pup_object_destroy_instance(struct PupObject *obj)
{
	// nothing do do
}

struct PupObject *pup_create_object(ENV, struct PupClass *type)
{
	ABORTF_ON(!type, "'type' must not be null");
	return pup_object_allocate(env, (struct PupObject *)type, 0, NULL);
}

void pup_object_destroy(struct PupObject *obj)
{
	pup_class_destroy_instance(obj->type, obj);

	/*
	struct PupAttributeListEntry *attr = obj->attr_list_head;
	while (attr) {
		struct PupAttributeListEntry *tmp = attr;
		attr = attr->next;
		free(tmp);
	}
	*/
}
/*
void pup_object_free(struct PupObject *obj)
{
	pup_object_destroy(obj);
	free(obj);
}
*/

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

/*
 * Caller must free() the result
 */
static char *sym_name(ENV, const int sym)
{
	const char *str = pup_env_sym_to_str(env, sym);
	if (str) {
		return strdup(str);
	}
	char buf[256];
	snprintf(buf, sizeof(buf), "sym:%d", sym);
	return strdup(buf);
}

struct PupObject *pup_invoke(ENV, struct PupObject *target, const long name_sym,
                             const long argc, struct PupObject **argv)
{
	ABORTF_ON(!target, "NULL target invoking `%s'", pup_env_sym_to_str(env, name_sym));
	struct PupClass *class = target->type;
	ABORTF_ON(!class, "NULL class invoking `%s' on object %p", pup_env_sym_to_str(env, name_sym), target);
	PupMethod *method = find_method_in_classes(class, name_sym);
	if (!method) {
		char *name = sym_name(env, name_sym);
		char buf[256];
		snprintf(buf, sizeof(buf), "undefined method `%s' for %s", name, pup_object_type_name(target));
		free(name);
		pup_raise_runtimeerror(env, buf);
	}
	return (*method)(env, target, argc, argv);
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

static struct PupAttributeListEntry *create_attr(ENV, const int sym,
                                              struct PupObject *val,
					      struct PupAttributeListEntry *next)
{
	struct PupAttributeListEntry *attr
		= pup_alloc_attr(env, sizeof(struct PupAttributeListEntry));
	attr->name_sym = sym;
	attr->value = val;
	attr->next = next;
	return attr;
}

void pup_iv_set(ENV, struct PupObject *obj,
                const int sym, struct PupObject *val)
{
	struct PupAttributeListEntry *attr = find_attr(obj, sym);
	// TODO: what to do about these race conditions?
	if (attr) {
		attr->value = val;
	} else {
		obj->attr_list_head =
			create_attr(env, sym, val, obj->attr_list_head);
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
struct PupClass *pup_class_context_from(ENV, struct PupObject *obj)
{
	if (pup_is_class_instance(env, obj)) {
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
	return pup_string_new_cstr(env, buf);
}

char *pup_stringify(ENV, struct PupObject *obj)
{
	if (pup_is_string(env, obj)) {
		return strdup(pup_string_value_unsafe(obj));
	}
	if (pup_instanceof_exception(env, obj)) {
		const char *msg = exception_text(env, obj);
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
	pup_arity_check(env, 1, argc);
	ABORT_ON(!argv, "puts() argv is NULL!");
	char *str = pup_stringify(env, argv[0]);
	puts(str);
	free(str);

	// TODO return nil
	return NULL;
}

METH_IMPL(pup_object_op_equals)
{
	pup_arity_check(env, 1, argc);
	struct PupObject *rhs = argv[0];
	return target == rhs  ? pup_env_get_trueinstance(env)
	                      : pup_env_get_falseinstance(env);
}

void pup_object_class_init(ENV, struct PupClass *class_obj)
{
	pup_define_method(class_obj,
	                  pup_env_str_to_sym(env, "initialize"),
	                  pup_object_initialize);
	pup_define_method(class_obj,
	                  pup_env_str_to_sym(env, "raise"),
	                  pup_object_raise);
	pup_define_method(class_obj,
	                  pup_env_str_to_sym(env, "puts"),
	                  pup_puts);
	pup_define_method(class_obj,
	                  pup_env_str_to_sym(env, "=="),
	                  pup_object_op_equals);
}
