#ifndef _OBJECT_H
#define _OBJECT_H

#include <stdbool.h>
#include "runtime.h"
#include "gc.h"

// struct PupObject needs to be visible so that it can be embedded in
// other structures

struct PupObject {
	struct PupClass *type;
	struct PupAttributeListEntry *attr_list_head;
	unsigned int gc_mark : 1;
};

struct PupClass *pup_bootstrap_create_classobject(ENV);

void obj_init(struct PupObject *obj, struct PupClass *type);

METH_IMPL(pup_object_allocate);

/**
 * Until we have instance methods, we assign these functions to a member of
 * struct PupClass
 */
struct PupObject *pup_object_allocate_instance(ENV, struct PupClass *type);

/**
 * GC cleanup
 */
void pup_object_destroy_instance(struct PupObject *obj);

/*
 * used to preserve objects which are still live during garbage collection
 */
struct PupObject *pup_object_gc_copy_instance(ENV, const struct PupObject *obj);

struct PupObject *pup_create_object(ENV, struct PupClass *type);

void pup_object_destroy(struct PupObject *obj);
//void pup_object_free(struct PupObject *obj);

const char *pup_object_type_name(const struct PupObject *obj);

bool pup_object_instanceof(const struct PupObject *obj,
                           const struct PupClass *class);

bool pup_object_kindof(const struct PupObject *obj,
                       const struct PupClass *class);

void pup_iv_set(ENV, struct PupObject *obj, const int sym, struct PupObject *val);

struct PupObject *pup_iv_get(struct PupObject *obj, const int sym);

/**
 * used during garbage collection
 */
void pup_object_each_ref(struct PupObject *obj,
                         void (*visitor)(struct PupObject **, void *),
                         void *data);

/**
 * returns true if the mark was updated, false if the object was already
 * marked.
 */
bool pup_object_gc_mark(struct PupObject *obj, int mark_value);
void pup_object_gc_collect(struct PupObject *obj, struct PupGCState *state);
void pup_object_attr_gc_collect(void *attr, struct PupGCState *state);
void pup_object_gc_mark_unconditionally(struct PupObject *obj, int mark_value);


struct PupObject *pup_invoke(ENV, struct PupObject *target, const long name_sym,
                             const long argc, struct PupObject **argv);

/*
 * Bootstrap the Object class. Used while initialising the runtime environment
 */
void pup_object_class_init(ENV, struct PupClass *class_obj);

#endif  // _OBJECT_H
