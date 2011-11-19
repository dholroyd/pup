
struct PupObject;

struct RuntimeEnv;

typedef struct PupObject *(PupMethod)(struct RuntimeEnv *, struct PupObject *, long, struct PupObject **);

struct PupClass;
