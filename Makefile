
clang=clang
llvmc=llvmc-2.9
tt=/var/lib/gems/1.8/gems/treetop-1.4.10/bin/tt

runit:	parser.rb runtime.o exception.o raise.o string.o class.o object.o symtable.o
	ruby -I tests tests/testsuite.rb


a.out:	test.bc runtime.bc exception.bc raise.bc string.bc
	llvm-ld-2.9 -disable-opt -disable-inlining -native test.bc runtime.bc exception.bc raise.bc string.bc

runtime.o:	runtime.c core_types.h abortf.h exception.h env.h
	${clang} -O0 -Wall -Werror -g -c -fexceptions runtime.c -o runtime.o

exception.o:	exception.c core_types.h abortf.h raise.h string.h object.h env.h
	${clang} -O0 -Wall -Werror -g -c -fexceptions exception.c -o exception.o

string.o:	string.c core_types.h runtime.h env.h
	${clang} -O0 -Wall -Werror -g -c -fexceptions string.c -o string.o

raise.o:	raise.c core_types.h abortf.h env.h
	${clang} -O0 -Wall -Werror -g -c -fexceptions raise.c -o raise.o

class.o:	class.c runtime.h string.h env.h
	${clang} -O0 -Wall -Werror -g -c -fexceptions class.c -o class.o

object.o:	object.c object.h class.h runtime.h exception.h string.h abortf.h env.h
	${clang} -O0 -Wall -Werror -g -c -fexceptions object.c -o object.o

symtable.o:	symtable.c
	${clang} -O0 -Wall -Werror -g -c -fexceptions symtable.c -o symtable.o

parser.rb:	parser.treetop
	${tt} parser.treetop
