
clang=clang
llvmc=llvmc-2.9
tt=/var/lib/gems/1.8/gems/treetop-1.4.10/bin/tt

runit:	parser.rb runtime.bc exception.bc raise.bc
	ruby -I tests tests/testsuite.rb


a.out:	test.bc runtime.bc exception.bc raise.bc
	llvm-ld-2.9 -disable-opt -disable-inlining -native test.bc runtime.bc exception.bc raise.bc

runtime.bc:	runtime.c core_types.h abortf.h exception.h
	${clang} -O0 -Wall -Werror -g -c -fexceptions -emit-llvm runtime.c -o runtime.bc

exception.bc:	exception.c core_types.h abortf.h raise.h
	${clang} -O0 -Wall -Werror -g -c -fexceptions -emit-llvm exception.c -o exception.bc

raise.bc:	raise.c core_types.h abortf.h
	${clang} -O0 -Wall -Werror -g -c -fexceptions -emit-llvm raise.c -o raise.bc

parser.rb:	parser.treetop
	${tt} parser.treetop
