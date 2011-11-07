
struct Object;

typedef struct Object *(Method)(struct Object *, long, struct Object **);

struct MethodListEntry {
	long name_sym;
	Method *method;
	struct MethodListEntry *next;
};

struct AttributeListEntry {
	long name_sym;
	struct Object *value;
	struct AttributeListEntry *next;
};

struct Class;

struct Object {
	struct Class *type;
	struct AttributeListEntry *attr_list_head;
};

struct Class {
	struct Object obj_header;
	struct Class *superclass;
	char *name;
	struct MethodListEntry *method_list_head;
};

struct String {
	struct Object obj_header;
	char *value;
};

struct Exception {
	struct Object obj_header;
	char *message;
	/* TODO: backtrace? */
};
