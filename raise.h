void pup_raise(struct Exception *pup_exception);

struct _Unwind_Exception *create_unwind_exception(
	struct Exception *pup_exception_obj
);

struct Exception *extract_exception_obj(
	const struct _Unwind_Exception *exceptionObject
);
