
#include <stdbool.h>

const char *pup_type_name(const struct PupClass *type);

PupMethod *find_method_in_classes(struct PupClass *class,
                                  const long name_sym);

bool pup_is_descendant_or_same(const struct PupClass *ancestor,
                              const struct PupClass *descendant);

bool pup_is_class_instance(ENV, const struct PupObject *obj);

struct PupObject *pup_internal_class_allocate_instance(ENV, struct PupClass *type);

struct PupObject *pup_class_allocate_instance(ENV, struct PupClass *clazz);

void pup_internal_class_init(ENV,
                             struct PupClass *class,
                             struct PupClass *superclass,
                             struct PupClass *scope,
                             const char *name,
                             struct PupObject *(*allocate_instance)(ENV, struct PupClass *));

struct PupClass *pup_internal_create_class(ENV,
                                  struct PupClass *superclass,
                                  struct PupClass *scope,
                                  const char *name,
                                  struct PupObject *(*allocate_instance)(ENV, struct PupClass *));

struct PupClass *pup_create_class(ENV,
                                  struct PupClass *superclass,
                                  struct PupClass *scope,
                                  const char *name);

struct PupClass *pup_bootstrap_create_classclass(ENV, struct PupClass *class_object);

void pup_define_method(struct PupClass *class, const long name_sym, PupMethod *method);

//void pup_class_free(struct PupClass *clazz);

void pup_const_set(ENV, struct PupClass* clazz, const int sym, struct PupObject *val);

/*
 * Bootstrap the Class class. Used while initialising the runtime environment
 */
void pup_class_class_init(ENV, struct PupClass *class_class);
