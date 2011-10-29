#define METH_IMPL(name) struct Object *name(struct Object *target, \
                                            long argc,             \
                                            struct Object **argv)

void obj_init(struct Object *obj, struct Class *type);

int pup_is_descendant_or_same(const struct Class *ancestor,
                              const struct Class *descendant);

const char *pup_type_name_of(const struct Object *obj);

struct Object *pup_string_new_cstr(char *str);

int pup_is_class(const struct Object *obj, const struct Class *class);

void pup_arity_check(int expected, int actual);
