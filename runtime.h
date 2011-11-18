#define METH_IMPL(name) struct PupObject *name(struct PupObject *target, \
                                               long argc,             \
                                               struct PupObject **argv)

void obj_init(struct PupObject *obj, struct PupClass *type);

struct PupObject *pup_create_object(struct PupClass *type);

const char *pup_type_name_of(const struct PupObject *obj);

int pup_is_class(const struct PupObject *obj, const struct PupClass *class);

void pup_arity_check(int expected, int actual);

void pup_iv_set(struct PupObject *obj, const int sym, struct PupObject *val);

struct PupObject *pup_iv_get(struct PupObject *obj, const int sym);
