
#include "env.h"
#include "object.h"
#include "raise.h"
#include "exception.h"
#include "core_types.h"
#include "class.h"

struct PupFixnum {
	struct PupObject obj_header;
	int value;
};

struct PupObject *pup_fixnum_create(ENV, int value)
{
	struct PupClass *fixclass = pup_env_get_classfixnum(env);
	struct PupObject *obj =
		(struct PupObject *)pup_alloc_obj(env, sizeof(struct PupFixnum));
	obj_init(obj, fixclass);
	struct PupFixnum *fix = (struct PupFixnum *)obj;
	fix->value = value;
	return obj;
}

static struct PupObject *fixnum_allocate_instance(ENV, struct PupClass *type)
{
	// this should really be impossible
	pup_raise_runtimeerror(env, "you can't call 'new' on Fixnum");
	abort();
}

static void fixnum_destroy_instance(struct PupObject *obj)
{
	// nothing to do
}

METH_IMPL(pup_fixnum_plus)
{
	pup_arity_check(env, 1, argc);
	struct PupObject *rhs = argv[0];
	if (target->type != rhs->type) {
		// TODO: TypeError
		pup_raise(pup_new_runtimeerrorf(env, "%s can't be coerced into Fixnum", pup_type_name(rhs->type)));
		abort();
	}
	struct PupFixnum *left = (struct PupFixnum *)target;
	struct PupFixnum *right = (struct PupFixnum *)rhs;
	// TODO: overflow -> bignum handling
	return pup_fixnum_create(env, left->value + right->value);
}

METH_IMPL(pup_fixnum_op_equals)
{
	pup_arity_check(env, 1, argc);
	struct PupObject *rhs = argv[0];
	if (target->type != rhs->type) {
		return pup_env_get_falseinstance(env);
	}
	struct PupFixnum *left = (struct PupFixnum *)target;
	struct PupFixnum *right = (struct PupFixnum *)rhs;
	return left->value == right->value  ? pup_env_get_trueinstance(env)
	                                    : pup_env_get_falseinstance(env);

}

struct PupClass *pup_bootstrap_create_classfixnum(ENV)
{
	struct PupClass *class_fix = pup_internal_create_class(env,
	                                 pup_env_get_classobject(env), // TODO: should be Numeric
	                                 NULL,  // no lexical scope
	                                 "Fixnum",
	                                 &fixnum_allocate_instance,
	                                 &fixnum_destroy_instance);
	pup_define_method(class_fix,
	                  pup_env_str_to_sym(env, "+"),
	                  pup_fixnum_plus);
	pup_define_method(class_fix,
	                  pup_env_str_to_sym(env, "=="),
	                  pup_fixnum_op_equals);
	return class_fix;
}


// TODO: remove 'new' from class methods of 'Fixnum' class instance
