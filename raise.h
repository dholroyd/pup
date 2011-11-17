void pup_raise(struct Object *pup_exception);

struct _Unwind_Exception *create_unwind_exception(
	struct Object *pup_exception_obj
);

struct Object *extract_exception_obj(
	const struct _Unwind_Exception *exceptionObject
);
