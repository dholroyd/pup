
#include <stdlib.h>
#include <stdio.h>
#include "../symtable.h"
#include "../abortf.h"

int main(int argc, char **argv)
{
	struct SymTable *st = pup_sym_table_create();
	int foobar_sym = pup_str_to_sym(st, "foobar");
	ABORT_ON(foobar_sym <= 0,
	         "expected greater than 0");
	int barblat_sym = pup_str_to_sym(st, "barblat");
	ABORT_ON(barblat_sym <= 0,
	         "expected greater than 0");
	ABORTF_ON(foobar_sym == barblat_sym,
	         "different strings should result in different symbols %d %d",
	         foobar_sym, barblat_sym);
	int tmp_sym;
	ABORT_ON(!pup_get_sym(st, "foobar", &tmp_sym),
	         "symbol retreval failed");
	ABORT_ON(tmp_sym != foobar_sym, "symbol value missmatch");
	ABORT_ON(!pup_get_sym(st, "barblat", &tmp_sym),
	         "symbol retreval failed");
	ABORT_ON(tmp_sym != barblat_sym, "symbol value missmatch");
	pup_sym_table_destroy(st);
	return 0;
}
