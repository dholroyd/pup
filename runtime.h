#define METH_IMPL(name) struct PupObject *name(struct PupObject *target, \
                                               long argc,             \
                                               struct PupObject **argv)

void obj_init(struct PupObject *obj, struct PupClass *type);

struct PupObject *pup_create_object(struct PupClass *type);

int pup_is_descendant_or_same(const struct PupClass *ancestor,
                              const struct PupClass *descendant);

const char *pup_type_name_of(const struct PupObject *obj);

struct PupObject *pup_string_new_cstr(const char *str);

int pup_is_class(const struct PupObject *obj, const struct PupClass *class);

void pup_arity_check(int expected, int actual);
