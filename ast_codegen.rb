
# extends AST classes with codegen() methods which build LLVM IR

require 'runtime'

module Pup
module Parse

class CodegenContext

  ObjectClassInstanceName = "ObjectClassInstance"
  StringClassInstanceName = "StringClassInstance"

  include ::Pup::Core::Types

  attr_accessor :invoker
  attr_reader :module, :self_ref, :runtime_builder

  def initialize
    @module = LLVM::Module.create("Pup")
    @build = nil
    @self_ref = nil
    @current_method = nil
    @block = nil
    @string_class_global = global_constant(ClassType, nil, StringClassInstanceName)
    @puts_method = build_puts_meth
    init_types

    # the sequence of numbers used to make sure LLVM function names generated
    # for method implementations are unique
    @meth_seq = 0

    @runtime_builder = ::Pup::Runtime::RuntimeBuilder.new(self)
    @runtime_builder.build_runtime_init
  end

  def global_constant(type, value, name="")
    const = @module.globals.add(type, name)
    const.linkage = :internal
    const.initializer = value if value
    const
  end

  def global_string_constant(value, name=nil)
    str = LLVM::ConstantArray.string(value)
    name = "str#{value}" unless name
    global_constant(str, str, name).bit_cast(CStrType)
  end

  def init_types
    object_name_ptr = global_string_constant("Object")

    @module.types.add("Object", ObjectType)
    @module.types.add("Class", ClassType)
    @module.types.add("String", StringObjectType)
    @module.types.add("Method", MethodType)
    @module.types.add("AttributeListEntry", AttributeListEntryType)
    @module.types.add("MethodListEntry", MethodListEntryType)

    class_class_fwd_decl = @module.globals.add(ClassType, "ClassClassInstance")
    class_class_fwd_decl.linkage = :internal
    
    object_class_instance = const_struct(
      # object header,
      const_struct(
	# class,
        @module.globals["ClassClassInstance"],
	# attribute list head
	AttributeListEntryType.pointer.null_pointer
      ),
      # superclass (none, for 'Object')
      ClassType.pointer.null_pointer,
      # class name,
      object_name_ptr,
      # method list head,
      MethodListEntryType.pointer.null_pointer
    )
    obj_class_global = global_constant(ClassType, object_class_instance, ObjectClassInstanceName)
    class_class_instance = build_class_instance("Class", obj_class_global)
    class_class_fwd_decl.initializer = class_class_instance
    string_class_instance = build_class_instance("String", obj_class_global)
    @string_class_global.initializer = string_class_instance


    true_class_instance = build_class_instance("TrueClass", obj_class_global)
    true_class_global = global_constant(ClassType, true_class_instance, "TrueClassInstance")
    false_class_instance = build_class_instance("FalseClass", obj_class_global)
    false_class_global = global_constant(ClassType, false_class_instance, "FalseClassInstance")

    true_obj_instance = const_struct(
      true_class_global,
      AttributeListEntryType.pointer.null
    )
    true_global = global_constant(ObjectType, true_obj_instance, "TrueObjInstance")
    false_obj_instance = const_struct(
      false_class_global,
      AttributeListEntryType.pointer.null
    )
    false_global = global_constant(ObjectType, false_obj_instance, "FalseObjInstance")

    main_class_instance = build_class_instance("Main", obj_class_global, build_meth_list_entry(:puts, @puts_method))
    global_constant(ClassType, main_class_instance, "Main")
  end

  def build_class_instance(class_name, super_class_instance, meth_list_head=nil)
    super_pointer = super_class_instance.nil? ? ClassType.pointer.null : super_class_instance
    const_struct(
      # object header,
      const_struct(
	# this.class
        @module.globals["ClassClassInstance"],
	# attribute list head
	AttributeListEntryType.pointer.null_pointer
      ),
      super_class_instance,
      # class name,
      global_string_constant(class_name),
      # method list head,
      meth_list_head || MethodListEntryType.pointer.null_pointer
    )
  end

  def const_struct(*members)
    LLVM::ConstantStruct.const(members.length) { |i| members[i] }
  end

  def append_block(name = "")
    last_block = @block
    result = @block = current_method.function.basic_blocks.append(name)
    begin
      yield
    ensure
      @block = last_block
    end
    result
  end

  def with_builder_at_end(block=@block)
    last_builder = @build
    @build = LLVM::Builder.create
    @build.position_at_end(block)
    begin
      yield @build
    ensure
      @build.dispose
      @build = last_builder
    end
  end

  def current_method
    raise "No current method" unless @current_method
    @current_method
  end

  def build
    raise "No current builder" unless @build
    @build
  end

  def eval_build(&b)
    build.instance_eval(&b)
  end

  def using_self(new_self)
    # TODO: assert that the given arg is of type acceptable for 'self'
    old_self = @self_ref
    begin
      @self_ref = new_self
      yield
    ensure
      @self_ref = old_self
    end
  end

  # a wrapper for def_method that makes an LLVM function name from the given
  # name in a way that ensures that it will not clash with the LLVM function
  # used to implement another pup method with the same name defined elsewhere
  def def_method_uniquely(name)
    def_method("meth_#{@meth_seq += 1}_#{name}") do |meth|
      yield meth
    end
  end

  def def_method(name)
    arg_types = [ObjectPtrType, LLVM::Int, ArgsType]
    @module.functions.add(name, arg_types, ObjectPtrType) do |fn, target, argc, argv|
      target.name = "target"
      argc.name = "argc"
      argv.name = "argv"
      last_method = @current_method
      last_self_ref = @self_ref
      begin
	@current_method = MethodRef.new(fn, target, argc, argv)
	@self_ref = target
	yield @current_method
      ensure
	@current_method = last_method
	@self_ref = last_self_ref
      end
    end
  end

  class MethodRef
    attr_reader :function, :param_target, :param_argc, :param_argv
    def initialize(function, param_target, param_argc, param_argv)
      @entry_block_builder = nil
      @function = function
      @param_target = param_target
      @param_argc = param_argc
      @param_argv = param_argv
      @locals = {}
    end

    def entry_block_builder
      return @entry_block_builder if @entry_block_builder
      build = LLVM::Builder.create
      blocks = function.basic_blocks
      blocks.append("entry") if blocks.size == 0
      entry = blocks.entry
      build.position_at_end(entry)
      @entry_block_builder = build
    end

    def get_or_create_local(name)
      local = get_local(name)
      unless local
	local = entry_block_builder.alloca(::Pup::Core::Types::ObjectPtrType, "local_#{name}")
	@locals[name] = local
      end
      local
    end

    def get_local(name)
      raise "missing name" unless name
      raise "not a String" unless String === name
      @locals[name]
    end

    def has_local?(name)
      @locals[name] != nil
    end
  end

  def build_real_main_fn
    @module.functions.add("main", [LLVM::Int32, LLVM.Pointer(CStrType)], LLVM::Int32) do |mainfn, argc, argv|
      argc.name = "argc"
      argv.name = "argv"
      entry = mainfn.basic_blocks.append
      with_builder_at_end(entry) do |b|
	main_obj = b.alloca(ObjectType, "main_obj")
	main_obj_class = b.struct_gep(main_obj, 0, "main_obj_class")
	b.store(@module.globals["Main"], main_obj_class)
	attr_list_head = b.struct_gep(main_obj, 1, "attr_list_head")
	b.store(AttributeListEntryType.pointer.null, attr_list_head)

	ret_val = build.call(@module.functions["pup_runtime_init"])

	ret_val = build.call(@module.functions["pup_main"], main_obj, LLVM.Int(0), ArgsType.null)
	b.ret(LLVM::Int32.from_i(0))
      end
    end
  end

  # helper for building method callsites
  #
  #  method_name - a ruby String - this method will convert to a symbol
  #  args - a ruby array of LLVM::Values - this method will allocate the
  #         LLVM array and store the args values into it
  def build_simple_method_invoke(receiver, method_name, *args)
    argc = LLVM::Int32.from_i(args.length)
    argv = build.array_alloca(::Pup::Core::Types::ObjectPtrType, argc, "#{method_name}_argv")
    args.each_with_index do |arg, i|
      arg_element = build.gep(argv, [LLVM.Int(i)], "#{method_name}_argv_#{i}")
      build.store(arg, arg_element)
    end
    build_simple_method_invoke_argv(receiver, method_name, argv, args.length)
  end

  # helper for building method callsites
  # 
  #  method_name - a ruby String (will convert to a symbol before use)
  #  argv - an LLVM array of arguments
  #  argc - ruby fixnum - the number of arguments in the argv array
  def build_simple_method_invoke_argv(receiver, method_name, argv, argc)
    sym = LLVM.Int(method_name.to_sym.to_i)
    build.call(invoker, receiver, sym, LLVM.Int(argc), argv, "#{method_name}_ret")
  end

  private

  def class_ptr_from_obj_ptr(builder, obj_ptr)
    raise "not a pointer to object" unless obj_ptr.type == ::Pup::Core::Types::ObjectPtrType
    obj_class = builder.struct_gep(obj_ptr, 0, "obj_class")
    return builder.load(obj_class, "class_p")
  end

  def gen_if_instance_of(builder, obj_p, class_p, then_bk, else_bk)
    objclass_p = class_ptr_from_obj_ptr(builder, obj_p)
    cmp2 = builder.icmp(:eq, objclass_p, class_p, "is_instance")
    builder.cond(cmp2, then_bk, else_bk)
  end

  def build_puts_meth
    putsf = @module.functions.add("puts", [CStrType], LLVM::Int32)
    # TODO: move!
    @module.functions.add("abort", [], LLVM.Void)
    @module.functions.add("printf", [CStrType], LLVM::Int, true)

    def_method("pup_puts") do |pup_puts|
      entry_build = pup_puts.entry_block_builder
      wrong_no_args = pup_puts.function.basic_blocks.append("wrong_no_args")
      test_str = pup_puts.function.basic_blocks.append("test_str")
      do_puts = pup_puts.function.basic_blocks.append("do_puts")
      bad_string = pup_puts.function.basic_blocks.append("bad_string")
      cmp1 = entry_build.icmp(:eq, pup_puts.param_argc, LLVM.Int(1))
      entry_build.cond(cmp1, test_str, wrong_no_args)

      with_builder_at_end(wrong_no_args) do |b|
	err_msg = global_string_constant("Wrong number of arguments given to puts()", "wrong_num_args")
	b.call(putsf, err_msg)
	b.ret(ObjectPtrType.null)
      end
      arg = nil
      with_builder_at_end(test_str) do |b|
	arg = b.load(pup_puts.param_argv, "arg")
	gen_if_instance_of(b, arg, @module.globals[StringClassInstanceName],
			   do_puts, bad_string)
      end

      with_builder_at_end(bad_string) do |b|
	err_msg = global_string_constant("puts() requires a String argument", "string_reqd")
	b.call(putsf, err_msg)
	b.ret(ObjectPtrType.null)
      end

      with_builder_at_end(do_puts) do |b|
	str = b.bit_cast(arg, StringObjectType.pointer, "str")
	cstr_p = b.struct_gep(str, 1, "cstr_p")
	cstr = b.load(cstr_p)
	b.call(putsf, cstr)
	b.ret(ObjectPtrType.null)
      end
    end
  end

  def build_meth_list_entry(name, fn_ptr)
    entry = const_struct(
      LLVM.Int(name.to_sym.to_i),
      fn_ptr,
      MethodListEntryPtrType.null_pointer
    )
    global_constant(MethodListEntryType, entry, "meth_entry_#{name}")
  end
end

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

  def var_access?(ctx)
    @args.nil? && @receiver.nil? && ctx.current_method.has_local?(@name.name)
  end

  def codegen_var_access(ctx)
    local = ctx.current_method.get_local(@name.name)
    ctx.build.load(local, "#{@name.name}_access")
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
    ctx.build.call(ctx.invoker, r, sym, LLVM::Int(arg_count), argv, "#{@name.name}_ret")
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
    ctx.eval_build do
      data = global_string(str)
      obj_ptr = malloc(::Pup::Core::Types::StringObjectType, "strliteral")
      strobj = gep(obj_ptr, LLVM.Int(0), "strobj")
      objobj = struct_gep(strobj, 0, "objobj")
      class_ptr = struct_gep(objobj, 0, "classptr")
      # set the class pointer within the object header,
      store(ctx.module.globals[::Pup::Parse::CodegenContext::StringClassInstanceName], class_ptr)

      data_ptr = struct_gep(strobj, 1, "data_ptr")
      src_ptr = gep(data, [LLVM.Int(0),LLVM.Int(0)], "src_ptr")
      store(src_ptr, data_ptr)
      objobj
    end
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
    classref = ctx.current_method.get_or_create_local(name.name)
    classdef = nil
    class_name = ctx.global_string_constant(name.name)
    superclass_ref = find_superclass(ctx)
    ctx.eval_build do
      classdef = call(ctx.module.functions["pup_create_class"],
                      ctx.module.globals["ClassClassInstance"],
		      superclass_ref,
		      class_name)
      classref = bit_cast(classref, ClassType.pointer.pointer)
      store(classdef, classref)
    end
    # self becomes a ref to the class being defined, within the class body,
    ctx.using_self(classdef) do
      body.codegen(ctx)
    end

    classref
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
  def codegen(ctx)
    fn = ctx.def_method_uniquely(name.name) do |fn|
      method_body = ctx.append_block("body") do
	ctx.with_builder_at_end do
	  if body
	    ret = body.codegen(ctx)
	  else
	    raise "TODO: return nil when method body is empty"
	  end
	  ctx.build.ret(ret)
	end
      end
    end
    # TODO; adding methods to the metaclass of an object?
    ctx.runtime_builder.call_define_method(ctx.self_ref, LLVM.Int(name.name.to_sym.to_i), fn)
  end
end

end
end
