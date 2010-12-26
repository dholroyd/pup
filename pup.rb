require 'parser_driver'
require 'core_types'
require 'ast_codegen'
require 'pp'
require 'llvm/bitcode'

name = ARGV[0]
outname = name.sub(/\.pup/, "") + ".bc"


ast = Pup::Parse.parse(File.read(name))

ctx = Pup::Parse::CodegenContext.new
ast.codegen(ctx)

ctx.build_real_main_fn

ctx.module.write_bitcode(outname)

$stderr.puts "wrote #{outname.inspect}"
