
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "core_types.h"

extern struct Class StringClassInstance;

static void obj_init(struct Object *obj, struct Class *type)
{
	obj->type = type;
	obj->attr_list_head = NULL;
}

struct Class *pup_create_class(struct Class *class_class,
                               struct Class *superclass,
			       const char *name)
{
	struct Class *class = (struct Class *)malloc(sizeof(struct Class));
	obj_init(&class->obj_header, class_class);
	class->name = strdup(name);
	class->superclass = superclass;
	class->method_list_head = NULL;
	return class;
}

/*
 * Adds the given Method to the method table of the given Class
 */
void pup_define_method(struct Class *class, const long name_sym, Method *method)
{
	struct MethodListEntry **pos;
	struct MethodListEntry *new;

	if (!class) {
		printf("Class reference given to pup_define_method() must not be null\n");
		abort();
	}
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



struct Object *pup_object_allocate(struct Object *target,
                                   long argc,
				   struct Object **argv)
{
	struct Object *obj = (struct Object *)malloc(sizeof(struct Object));
	obj->type = (struct Class *)target;
	obj->attr_list_head = NULL;
	return obj;
}

/*
 * Default implementation for Object#initialize
 */
struct Object *pup_object_initialize(struct Object *target,
                                     long argc,
                                     struct Object **argv)
{
	// TODO: return nil
	return NULL;
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
	struct Class *class = target->type;
	Method *method = find_method_in_classes(class, name_sym);
	if (!method) {
		printf("No method sym:%ld in class <%s>\n",
		       name_sym, class->name);
		abort();
	}
	return (*method)(target, argc, argv);
}

int pup_is_class(const struct Object *obj, const struct Class *class)
{
	return obj->type == class;
}

static const char *type_name_of(const struct Object *obj)
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

struct Object *pup_puts(const struct Object *target,
                        const long argc,
                        const struct Object **argv)
{
	struct String *arg;
	if (argc != 1) {
		printf("Wrong number of arguments for puts(), %ld, expecting 1\n",
		       argc);
		// TODO return nil
		return NULL;
	}
	if (!argv) {
		printf("puts() argv is NULL!\n");
		abort();
	}
	if (!pup_is_class(argv[0], &StringClassInstance)) {
		printf("Argument to puts must be a %s, but got class %s\n",
		       StringClassInstance.name, type_name_of(argv[0]));
		abort();
	}
	arg = (struct String *)argv[0];
	if (!arg) {
		printf("String object contains NULL data pointer!\n");
	}
	puts(arg->value);

	// TODO return nil
	return NULL;
}
