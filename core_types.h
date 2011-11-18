
struct PupObject;

typedef struct PupObject *(PupMethod)(struct PupObject *, long, struct PupObject **);

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
