
#include <stdbool.h>

struct SymTable;

struct SymTable *pup_sym_table_create();

void pup_sym_table_destroy(struct SymTable *st);

/*
 * Note the string given is not duplicated, a reference to the string is
 * kept in the symbol table, and must not be freed prior to the symbol table
 * itself being freed.
 */
int pup_str_to_sym(struct SymTable *st, char *str);
// TODO: might be best to change the above noted behevior

const char *pup_sym_to_str(struct SymTable *st, int sym);

bool pup_get_sym(struct SymTable *st, char *str, int *result_sym);
