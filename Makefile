
clang=clang
llvmc=llvmc
llvmld=llvm-ld

runit:	a.out
	./a.out


a.out:	test.bc runtime.bc
	${llvmld} -native test.bc runtime.bc

test.bc:	test.pup *.rb
	./pup test.pup

runtime.bc:	runtime.c core_types.h
	${clang} -g -c -emit-llvm runtime.c -o runtime.bc
