
CC = clang
check = valgrind --quiet --error-exitcode=1 --leak-check=full

tests:	check_symtable_test check_env_test

check_symtable_test:	symtable_test
	${check} ./symtable_test

symtable_test:	symtable_test.c ../symtable.c ../symtable.h
	${CC} -g -Wall -Werror symtable_test.c ../symtable.c -o symtable_test

check_env_test:	env_test
	${check} ./env_test

heap_test:	heap_test.c ../heap.c
	${CC} -g -Wall -Werror -pthreads heap_test.c ../heap.c -o heap_test

check_heap_test:	heap_test
	${check} ./heap_test

env_test:	env_test.c ../env.c ../env.h
	${CC} -g -Wall -Werror env_test.c ../env.c ../symtable.c ../class.c ../object.c ../exception.c ../raise.c ../string.c ../runtime.c -o env_test
