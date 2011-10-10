
clang=clang
llvmc=llvmc-2.8
tt=/var/lib/gems/1.8/gems/treetop-1.4.8/bin/tt

runit:	a.out
	./a.out


a.out:	test.bc runtime.bc
	${llvmc} -g test.bc runtime.bc

test.bc:	test.pup *.rb
	./pup test.pup

runtime.bc:	runtime.c core_types.h
	${clang} -g -c -emit-llvm runtime.c -o runtime.bc

parser.rb:	parser.treetop
	${tt} parser.treetop
