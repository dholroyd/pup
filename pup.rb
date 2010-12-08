
require 'parser_driver'
require 'core_types'
require 'ast_codegen'
require 'pp'

ast = Pup::Parse.parse(File.read(ARGV[0]))

ctx = Pup::Parse::CodegenContext.new

ast.codegen(ctx)

ctx.build_real_main_fn

#ctx.module.verify
ctx.module.dump
