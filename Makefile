
clang=clang
llvmc=llvmc-2.9
tt=/var/lib/gems/1.8/gems/treetop-1.4.10/bin/tt

runit:	a.out
	valgrind -q ./a.out


a.out:	test.bc runtime.bc
	llvm-ld-2.9 -disable-inlining -native test.bc runtime.bc

test.bc:	test.pup *.rb
	./pup test.pup

runtime.bc:	runtime.c core_types.h abortf.h
	${clang} -O0 -g -c -emit-llvm runtime.c -o runtime.bc

parser.rb:	parser.treetop
	${tt} parser.treetop
