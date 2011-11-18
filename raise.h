void pup_raise(struct PupObject *pup_exception);

struct _Unwind_Exception *create_unwind_exception(
	struct PupObject *pup_exception_obj
);

struct PupObject *extract_exception_obj(
	const struct _Unwind_Exception *exceptionObject
);
