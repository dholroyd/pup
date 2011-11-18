
#include <stdbool.h>

struct PupObject *pup_string_new_cstr(const char *str);

/**
 * Returns the C string value from the given object, without a runtime check
 * that the given object is actually a 'struct PupString' (caller must check
 * this).
 */
const char *pup_string_value_unsafe(struct PupObject *str);

const char *pup_string_value(struct PupObject *str);

bool pup_is_string(struct PupObject *obj);
