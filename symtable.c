
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// a *highly* naive symbol table implementation 

struct SymTableEntry {
	int sym;
	char *str;
	struct SymTableEntry *next;
};

struct SymTable {
	int next_sym;
	struct SymTableEntry *head;
};

struct SymTable *pup_sym_table_create()
{
	struct SymTable *st = malloc(sizeof(struct SymTable));
	if (!st) {
		return NULL;
	}
	st->next_sym = 1;
	st->head = NULL;
	return st;
}

void pup_sym_table_destroy(struct SymTable *st)
{
	struct SymTableEntry *entry = st->head;
	while (entry) {
		struct SymTableEntry *tmp = entry;
		entry = entry->next;
		free(tmp);
	}
	free(st);
}

static struct SymTableEntry *entry_create(int sym, char *str)
{
	struct SymTableEntry *entry = malloc(sizeof(struct SymTableEntry));
	if (!entry) {
		return NULL;
	}
	entry->sym = sym;
	entry->str = str;
	entry->next = NULL;
	return entry;
}

static void find_entry(char *str, struct SymTableEntry ***entryp)
{
	while (**entryp) {
		struct SymTableEntry *entry = **entryp;
		if (!strcmp(str, entry->str)) {
			return;
		}
		*entryp = &entry->next;
	}
}

int pup_str_to_sym(struct SymTable *st, char *str)
{
	struct SymTableEntry **entryp = &st->head;
	find_entry(str, &entryp);
	if (*entryp) {
		return (*entryp)->sym;
	}
	*entryp = entry_create(st->next_sym++, str);
	return (*entryp)->sym;
}

bool pup_get_sym(struct SymTable *st, char *str, int *result_sym)
{
	struct SymTableEntry **entryp = &st->head;
	find_entry(str, &entryp);
	if (*entryp) {
		*result_sym = (*entryp)->sym;
		return true;
	}
	return false;
}

const char *pup_sym_to_str(struct SymTable *st, int sym)
{
	struct SymTableEntry *entry = st->head;
	while (entry) {
		if (entry->sym == sym) {
			return entry->str;
		}
		entry = entry->next;
	}
	return NULL;
}


