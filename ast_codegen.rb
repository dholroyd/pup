
# extends AST classes with codegen() methods which build LLVM IR

require 'runtime'
require 'codegen_context'

module Pup
module Parse

class Unit
  def codegen(ctx)
    ctx.def_method("pup_main") do |main_def|
      entry_build = main_def.entry_block_builder
      body = ctx.append_block("body") do
	ctx.with_builder_at_end do
	  last = stmts.codegen(ctx)
	  ctx.build.ret(last)
	end
      end
      entry_build.br(body)
    end
  end
end

class Statements
  def codegen(ctx)
    last = nil
    each{|s| last = s.codegen(ctx)}
    last
  end
end

class IfStmt
  def codegen(ctx)
    # alloacte a place to store the 'value' of the if-stmt,
    result = ctx.current_method.entry_block_builder.alloca(::Pup::Core::Types::ObjectPtrType, "if_tmp_value")

    bkcond = ctx.current_method.function.basic_blocks.append("cond")
    ctx.build.br(bkcond)
    bkthen = ctx.current_method.function.basic_blocks.append("then")
    bkelse = ctx.current_method.function.basic_blocks.append("else")
    bkcontinue = ctx.current_method.function.basic_blocks.append("ifcontinue")
    ctx.with_builder_at_end(bkcond) do |b|
      val = cond.codegen(ctx)
      cmp = b.icmp(:eq, val, ctx.build_call.pup_env_get_falseinstance(ctx.current_method.env), "is_false")
      b.cond(cmp, bkelse, bkthen)
    end
    ctx.with_builder_at_end(bkthen) do |b|
      v = ifbk.codegen(ctx)
      b.store(v, result)
      b.br(bkcontinue)
    end
    if elsebk
      ctx.with_builder_at_end(bkelse) do |b|
	v = elsebk.codegen(ctx)
	b.store(v, result)
	b.br(bkcontinue)
      end
    else
      raise "write code to make the 'result' of the if-stmt be nil"
    end
    ctx.build.position_at_end(bkcontinue)
    ctx.build.load(result, "if_result")
  end
end

class WhileStmt
  def codegen(ctx)
    # alloacte a place to store the 'value' of the if-stmt,
    result = ctx.current_method.entry_block_builder.alloca(::Pup::Core::Types::ObjectPtrType, "while_tmp_value")
    # TODO: write nil to while_tmp_value

    bkcond = ctx.current_method.function.basic_blocks.append("while_cond")
    ctx.build.br(bkcond)
    bkbody = ctx.current_method.function.basic_blocks.append("while_body")
    bkcontinue = ctx.current_method.function.basic_blocks.append("while_continue")

    ctx.with_builder_at_end(bkcond) do |b|
      val = cond.codegen(ctx)
      cmp = b.icmp(:eq, val, ctx.build_call.pup_env_get_falseinstance(ctx.current_method.env), "is_false")
      b.cond(cmp, bkcontinue, bkbody)
    end
    ctx.with_builder_at_end(bkbody) do |b|
      v = statements.codegen(ctx)
      b.store(v, result)
      ctx.build_call.pup_env_safepoint(ctx.current_method.env)
      b.br(bkcond)
    end
    ctx.build.position_at_end(bkcontinue)
    ctx.build.load(result, "if_result")
  end
end

class AssignExpr
  def codegen(ctx)
    case left
      when NameExpr
	codegen_local(ctx)
      when InstVarExpr
	codegen_instance(ctx)
      else
	raise "unhandled lhs: #{left.class.name}"
    end
  end

  def codegen_local(ctx)
    lhs = ctx.current_method.get_or_create_local(left.name)
    rhs = right.codegen(ctx)
    ctx.build.store(rhs, lhs)
    rhs
  end

  def codegen_instance(ctx)
    rhs = right.codegen(ctx)
    ctx.build_call.pup_iv_set(ctx.current_method.env, ctx.self_ref,
                              ctx.mk_sym(left.name), rhs)
    rhs
  end
end

class VarOrInvokeExpr
  def codegen(ctx)
    if var_access?(ctx)
      codegen_var_access(ctx)
    else
      codegen_invoke(ctx)
    end
  end

  def const?
    ConstantNameExpr === @name
  end

  def var_access?(ctx)
    @args.nil? && @receiver.nil? && (ctx.current_method.has_local?(@name.name) || const?)
  end

  def codegen_var_access(ctx)
    if const?
      self_class = ctx.build_call.pup_class_context_from(ctx.current_method.env, ctx.self_ref, "self_class")
      ctx.build_call.pup_const_get_required(ctx.current_method.env, self_class, ctx.mk_sym(@name.name), "#{@name.name}_access")
    else
      local = ctx.current_method.get_local(@name.name)
      ctx.build.load(local, "#{@name.name}_access")
    end
  end

  def codegen_invoke(ctx)
    # TODO: varargs
    r = @receiver ? @receiver.codegen(ctx) : ctx.self_ref

    arg_list = @args ? @args.map {|a| a.codegen(ctx) } : []
    ctx.build_method_invocation(r, name.name, *arg_list)
  end
end

class InstVarExpr
  def codegen(ctx)
    ctx.build_call.pup_iv_get(ctx.self_ref,
                              ctx.mk_sym(name))
  end
end

class SelfExpr
  def codegen(ctx)
    ctx.self_ref
  end
end

class AbstractBinaryExpr
  def codegen(ctx)
    codegen_binary(ctx, left.codegen(ctx), right.codegen(ctx))
  end
end

class EqualityExpr
  def codegen_binary(ctx, lhs, rhs)
    ctx.build_method_invocation(lhs, "==", rhs)
  end
end

class RelationalExpr
  def codegen_binary(ctx, lhs, rhs)
    ctx.build_method_invocation(lhs, op.to_s, rhs)
  end
end

class AddExpr
  def codegen_binary(ctx, lhs, rhs)
    ctx.build_method_invocation(lhs, "+", rhs)
  end
end

class StringLiteral
  def codegen(ctx)
    str = value
    me = self
    ctx.eval_build do
      data = global_string(str)
      src_ptr = gep(data, [LLVM.Int(0),LLVM.Int(0)], "src_ptr")
      ctx.build_call.pup_string_new_cstr(ctx.current_method.env,
                                         src_ptr, "str_#{me.sanitise(str)}")
    end
  end

  # derive a (shortened) string safe for use as an LLVM variable name
  def sanitise(str)
    str.gsub(/[^a-zA-Z0-1]+/, "_")[0, 20]
  end
end

class IntLiteral
  def codegen(ctx)
    ctx.build_call.pup_fixnum_create(ctx.current_method.env, LLVM.Int(@source.to_i))
  end
end

class BoolLiteral
  def codegen(ctx)
    if true?
      ctx.build_call.pup_env_get_trueinstance(ctx.current_method.env)
    else
      ctx.build_call.pup_env_get_falseinstance(ctx.current_method.env)
    end
  end
end

class ClassDef
  include ::Pup::Core::Types

  def codegen(ctx)
    # TODO: should be const, not local (implement const support)
    class_name = name.name
    classdef = nil
    class_name_ref = ctx.global_string_constant(class_name)
    superclass_ref = find_superclass(ctx)
    ctx.eval_build do
      self_class = ctx.build_call.pup_class_context_from(ctx.current_method.env, ctx.self_ref, "self_class")
      classdef = ctx.build_call.pup_create_class(ctx.current_method.env, 
		      superclass_ref,
                      self_class,
		      class_name_ref,
                      "class_#{class_name}")
      ctx.build_call.pup_const_set(ctx.current_method.env, self_class, ctx.mk_sym(class_name), bit_cast(classdef, ObjectPtrType))
    end
    # self becomes a ref to the class being defined, within the class body,
    ctx.using_self(classdef) do
      body.codegen(ctx)
    end

    classdef
  end

  def find_superclass(ctx)
    if extends
      throw "TODO: implement lookup of superclass by name (i.e. #{extends.inspect})"
    else
      ctx.build_call.pup_env_get_classobject(ctx.current_method.env)
    end
  end
end

class MethodDef
  include ::Pup::Core::Types

  def codegen(ctx)
    mangled_name = "pup_method__#{name.name}_#{ctx.next_serial}"
    fn = ctx.def_method(mangled_name) do |meth|
      # TODO: need to actually handle formal param list
      ctx.append_block do
	ctx.with_builder_at_end do
	  if body
            codegen_args(ctx, meth) if params
	    ret = body.codegen(ctx)
	    ctx.build.ret(ret)
	  else
            # TODO: return nil
	    ctx.build.ret(ObjectPtrType.null)
	  end
	end
      end
    end
    #hopefully_a_class = class_context(ctx)
    ctx.runtime_builder.call_define_method(ctx.self_ref, ctx.mk_sym(name.name), fn)
  end

  def codegen_args(ctx, meth)
    # FIXME: check meth.param_argc vs. params.size
    # TODO: copying references that already exist in argv is inefficient,
    #       but means directly reusing get_or_create_local() without having
    #       to think too much.  Do better than this.
    params.each_with_index do |a, i|
      var = meth.get_or_create_local(a.name.name)
      argp = ctx.build.gep(meth.param_argv, [LLVM.Int(i)])
      arg = ctx.build.load(argp)
      ctx.build.store(arg, var)
    end
  end

  def class_context(ctx)
    val = ctx.self_ref
    if val.type == ClassType.pointer
      return val
    end
    when_class = ctx.current_method.function.basic_blocks.append("when_class")
    when_not_class = ctx.current_method.function.basic_blocks.append("when_not_class")
    ctx.gen_if_instance_of(ctx.build,
                           val,
                           ctx.build_call.pup_env_get_classclass(ctx.current_method.env),
                           when_class, when_not_class)
    ctx.with_builder_at_end(when_not_class) do |b|
      ctx.build_call.puts(ctx.global_string_constant("'self' is not a Class instance"))
    end
    ctx.build.position_at_end(when_class)
    return ctx.build.bit_cast(val, ClassType.pointer)
  end
end

class BeginStmt
  def codegen(ctx)
    # alloacte a place to store the 'value' of the begin-stmt,
    result = ctx.current_method.entry_block_builder.alloca(::Pup::Core::Types::ObjectPtrType, "begin_tmp_value")
    landing_pad = nil
    if !rescuebks.empty?
      landingpad = ctx.current_method.function.basic_blocks.append("landingpad")
    end

    begin_block = nil
    ctx.eh_begin(landingpad) do
      v = beginbk.codegen(ctx)
      ctx.build.store(v, result)
      begin_block = ctx.build.insert_block
      # will add a br to after_beginblock later, once that block has been appended
    end
    dwarf_ex = nil
    rescue_impls = []
    ctx.with_builder_at_end(landingpad) do |b|
      dwarf_ex = b.call(ctx.module.functions["llvm.eh.exception"], "dwarf_ex")
      sel = b.call(ctx.module.functions["llvm.eh.selector"],
		   dwarf_ex,
		   ctx.module.functions["pup_eh_personality"].bit_cast(LLVM::Int8.type.pointer),
                   ctx.eh_catchall, "sel")
      excep = ctx.build_call.extract_exception_obj(dwarf_ex, "excep")
      excep_type = ctx.build.load(ctx.build.struct_gep(excep, 0), "excep_type")
      excep_type_asobj = ctx.build.bit_cast(excep_type, ::Pup::Core::Types::ObjectPtrType, "excep_type_asobj")
      ctx.eh_handle(excep) do
	rescuebks.each do |rescuebk|
	  rescuebk.codegen(ctx, result, excep, excep_type_asobj, rescue_impls)
	end
      end
      ctx.build_call.pup_rethrow_uncaught_exception(dwarf_ex)
      ctx.build.ret(::Pup::Core::Types::ObjectPtrType.null)
    end

    bkout = ctx.current_method.function.basic_blocks.append("after_beginblock")
    rescue_impls.each do |rescue_impl_bk|
      ctx.build.position_at_end(rescue_impl_bk)
      ctx.build.br(bkout)
    end
    ctx.build.position_at_end(begin_block)
    ctx.build.br(bkout)
    ctx.build.position_at_end(bkout)
    ctx.build.load(result, "begin_result")
  end
end

class RescueBlock
  def codegen(ctx, result, excep, excep_type_asobj, rescue_impls)
    bknot_matched = nil
    rescue_types = types || [VarOrInvokeExpr.new(ConstantNameExpr.new("RuntimeError"), nil, nil)]
    type_tests = []
    rescue_types.each do |ex_type|
      expected_type = ex_type.codegen(ctx)
      type_match = ctx.build_call.pup_is_descendant_or_same(expected_type, excep_type_asobj, "type_match")
      bknot_matched = ctx.current_method.function.basic_blocks.append(block_name_not_matched(ex_type))
      # we will append a 'br' to each block once we've created the
      # after_beginblock block that will be the branch target,
      type_tests << [ctx.build.insert_block, type_match, bknot_matched]
      ctx.build.position_at_end(bknot_matched)
    end
    next_rescue_block = ctx.build.insert_block
    bkrescue = ctx.current_method.function.basic_blocks.append("rescue_impl")
    rescue_impls << bkrescue
    type_tests.each do |block, type_match, bknot_matched|
      ctx.build.position_at_end(block)
      ctx.build.cond(type_match, bkrescue, bknot_matched)
    end
    ctx.build.position_at_end(bkrescue)
    if var_name
      var = ctx.current_method.get_or_create_local(var_name.name)
      ctx.build.store(excep, var)
    end
    v = statements.codegen(ctx)
    ctx.build.store(v, result)

    ctx.build.position_at_end(next_rescue_block)
  end

  # derive a block name that will be nicely readable in the common case that
  # the exception type spec is in terms of a const (i.e. an Exception class name)
  def block_name_not_matched(ex_type)
    if VarOrInvokeExpr === ex_type && ConstantNameExpr === ex_type.name
      "rescue_type_not_#{ex_type.name.name}"
    else
      "rescue_type_not_matched"
    end
  end
end

end  # module Parse
end  # module Pup
