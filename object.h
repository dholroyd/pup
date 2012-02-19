#include <stdbool.h>
#include "runtime.h"

// struct PupObject needs to be visible so that it can be embedded in
// other structures

struct PupObject {
	struct PupClass *type;
	struct PupAttributeListEntry *attr_list_head;
};

struct PupClass *pup_bootstrap_create_classobject(ENV);

void obj_init(struct PupObject *obj, struct PupClass *type);

METH_IMPL(pup_object_allocate);

/**
 * Until we have instance methods, we assign these functions to a member of
 * stryct PupClass
 */
struct PupObject *pup_object_allocate_instance(ENV, struct PupClass *type);

struct PupObject *pup_create_object(ENV, struct PupClass *type);

//void pup_object_destroy(struct PupObject *obj);
//void pup_object_free(struct PupObject *obj);

const char *pup_object_type_name(const struct PupObject *obj);

bool pup_object_instanceof(const struct PupObject *obj,
                           const struct PupClass *class);

bool pup_object_kindof(const struct PupObject *obj,
                       const struct PupClass *class);

void pup_iv_set(ENV, struct PupObject *obj, const int sym, struct PupObject *val);

struct PupObject *pup_iv_get(struct PupObject *obj, const int sym);

struct PupObject *pup_invoke(ENV, struct PupObject *target, const long name_sym,
                             const long argc, struct PupObject **argv);

/*
 * Bootstrap the Object class. Used while initialising the runtime environment
 */
void pup_object_class_init(ENV, struct PupClass *class_obj);
