
CC = clang
check = valgrind --quiet --error-exitcode=1 --leak-check=yes

check_symtable_test: symtable_test
	${check} ./symtable_test

symtable_test:	symtable_test.c ../symtable.c ../symtable.h
	${CC} -Wall -Werror symtable_test.c ../symtable.c -o symtable_test
