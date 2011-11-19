#include <stdbool.h>
#include "runtime.h"

// struct PupObject needs to be visible so that it can be embedded in
// other structures

struct PupObject {
	struct PupClass *type;
	struct PupAttributeListEntry *attr_list_head;
};

void obj_init(struct PupObject *obj, struct PupClass *type);

METH_IMPL(pup_object_allocate);

struct PupObject *pup_create_object(ENV, struct PupClass *type);

const char *pup_object_type_name(const struct PupObject *obj);

bool pup_object_instanceof(const struct PupObject *obj,
                           const struct PupClass *class);

bool pup_object_kindof(const struct PupObject *obj,
                       const struct PupClass *class);

void pup_iv_set(struct PupObject *obj, const int sym, struct PupObject *val);

struct PupObject *pup_iv_get(struct PupObject *obj, const int sym);
