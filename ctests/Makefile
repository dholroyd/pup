
CC = clang
check = valgrind --quiet --error-exitcode=1 --leak-check=yes

tests:	check_symtable_test check_env_test

check_symtable_test:	symtable_test
	${check} ./symtable_test

symtable_test:	symtable_test.c ../symtable.c ../symtable.h
	${CC} -g -Wall -Werror symtable_test.c ../symtable.c -o symtable_test

check_env_test:	env_test
	${check} ./env_test

env_test:	env_test.c ../env.c ../env.h
	${CC} -g -Wall -Werror env_test.c ../env.c ../symtable.c ../class.c ../object.c ../exception.c ../raise.c ../string.c ../runtime.c -o env_test
