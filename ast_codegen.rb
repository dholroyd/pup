
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
      cmp = b.icmp(:eq, val, ctx.module.globals["FalseObjInstance"], "is_false")
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

class AssignExpr
  def codegen(ctx)
    case left
      when NameExpr
	codegen_local(ctx)
      else
	raise "unhandled lhs: #{left.class.name}"
    end
  end

  def codegen_local(ctx)
    lhs = ctx.current_method.get_or_create_local(left.name)
    rhs = right.codegen(ctx)
    ctx.build.store(rhs, lhs)
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
      self_class = ctx.build.call(ctx.module.functions["pup_class_context_from"], ctx.self_ref, "self_class")
      ctx.build.call(ctx.module.functions["pup_const_get_required"], self_class, LLVM.Int(@name.name.to_sym.to_i), "#{@name.name}_access")
    else
      local = ctx.current_method.get_local(@name.name)
      ctx.build.load(local, "#{@name.name}_access")
    end
  end

  def codegen_invoke(ctx)
    # TODO: varargs
    # TODO: duplication vs. ctx.build_simple_invoke()
    r = @receiver ? @receiver.codegen(ctx) : ctx.self_ref

    argc = LLVM::Int32.from_i(arg_count)
    if @args
      argv = ctx.build.array_alloca(::Pup::Core::Types::ObjectPtrType, argc, "#{@name.name}_argv")
      @args.each_with_index do |arg, i|
	a = arg.codegen(ctx)
	arg_element = ctx.build.gep(argv, [LLVM.Int(i)], "#{@name.name}_argv_#{i}")
	ctx.build.store(a, arg_element)
      end
    else
      argv = ::Pup::Core::Types::ObjectPtrType.pointer.null
    end
    sym = LLVM.Int(name.name.to_sym.to_i)
    if ctx.eh_active?
      # block following invocation; continue here if no exception raised
      bkcontinue = ctx.current_method.function.basic_blocks.append("#{@name.name}_continue")
      res = ctx.build.invoke(ctx.invoker,
                       [r, sym, LLVM::Int(arg_count), argv],
                       bkcontinue, ctx.landingpad,
		       "#{@name.name}_ret")
      ctx.build.position_at_end(bkcontinue)
      res
    else
      ctx.build.call(ctx.invoker,
                     r, sym, LLVM::Int(arg_count), argv,
		     "#{@name.name}_ret")
    end
  end

  def arg_count
    @args ? @args.length : 0
  end
end

class SelfExpr
  def codegen(ctx)
    ctx.self_ref
  end
end

class StringLiteral
  def codegen(ctx)
    str = value
    me = self
    ctx.eval_build do
      data = global_string(str)
      src_ptr = gep(data, [LLVM.Int(0),LLVM.Int(0)], "src_ptr")
      call(ctx.module.functions["pup_string_create"], src_ptr, "str_#{me.sanitise(str)}")
    end
  end

  # derive a (shortened) string safe for use as an LLVM variable name
  def sanitise(str)
    str.gsub(/[^a-zA-Z0-1]+/, "_")[0, 20]
  end
end

class IntLitaeral
  # TODO
end

class BoolLiteral
  def codegen(ctx)
    if true?
      ctx.module.globals["TrueObjInstance"]
    else
      ctx.module.globals["FalseObjInstance"]
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
      self_class = call(ctx.module.functions["pup_class_context_from"], ctx.self_ref, "self_class")
      classdef = call(ctx.module.functions["pup_create_class"],
                      ctx.module.globals["ClassClassInstance"],
		      superclass_ref,
                      self_class,
		      class_name_ref,
                      "class_#{class_name}")
      call(ctx.module.functions["pup_const_set"], self_class, LLVM.Int(class_name.to_sym.to_i), bit_cast(classdef, ObjectPtrType))
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
      ctx.module.globals["ObjectClassInstance"]
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
    ctx.runtime_builder.call_define_method(ctx.self_ref, LLVM.Int(name.name.to_sym.to_i), fn)
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
                           ctx.module.globals["ClassClassInstance"],
                           when_class, when_not_class)
    ctx.with_builder_at_end(when_not_class) do |b|
      b.call(ctx.module.functions["puts"], ctx.global_string_constant("'self' is not a Class instance"))
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
    # block for code following the begin
    if ensurebk
      bkensure = ctx.current_method.function.basic_blocks.append("ensure")
      bkcontinue = ctx.current_method.function.basic_blocks.append("continue")
      #  branch to ensure code from begin/rescue blocks
      bkout = bkensure
    else
      bkensure = nil
      bkcontinue = ctx.current_method.function.basic_blocks.append("continue")
      # branch to code following begin-stmt from begin/rescue blocks
      bkout = bkcontinue
    end

    ctx.eh_begin(landingpad) do
      v = beginbk.codegen(ctx)
      ctx.build.store(v, result)
      ctx.build.br(bkout)
    end
    ctx.with_builder_at_end(landingpad) do |b|
      excep = b.call(ctx.module.functions["llvm.eh.exception"], "excep")
      sel = b.call(ctx.module.functions["llvm.eh.selector"],
		   excep,
		   ctx.module.functions["pup_eh_personality"].bit_cast(LLVM::Int8.type.pointer),
		   ctx.module.globals["ExceptionClassInstance"], "sel")
      ctx.eh_handle(excep) do
	rescuebks.first.codegen(ctx)
      end
      b.br(bkout)
    end
    ctx.build.position_at_end(bkout)
    ctx.build.load(result, "begin_result")
  end
end

class RescueBlock
  def codegen(ctx)
    if var_name
      var = ctx.current_method.get_or_create_local(var_name.name)
      excep = ctx.build.call(ctx.module.functions["extract_exception_obj"], ctx.excep)
      ctx.build.store(excep, var)
    end
    statements.codegen(ctx)
  end
end

end  # module Parse
end  # module Pup
