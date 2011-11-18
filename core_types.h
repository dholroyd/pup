
struct PupObject;

typedef struct PupObject *(PupMethod)(struct PupObject *, long, struct PupObject **);

struct PupMethodListEntry {
	long name_sym;
	PupMethod *method;
	struct PupMethodListEntry *next;
};

struct PupAttributeListEntry {
	long name_sym;
	struct PupObject *value;
	struct PupAttributeListEntry *next;
};

struct PupClass;

struct PupObject {
	struct PupClass *type;
	struct PupAttributeListEntry *attr_list_head;
};

struct PupClass {
	struct PupObject obj_header;
	struct PupClass *superclass;
	char *name;
	struct PupMethodListEntry *method_list_head;
	struct PupClass *scope;  /* for Constant lookup */
};

struct PupString {
	struct PupObject obj_header;
	char *value;
};
