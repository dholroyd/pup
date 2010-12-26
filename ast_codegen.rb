
# extends AST classes with codegen() methods which build LLVM IR

module Pup
module Parse

class CodegenContext

  ObjectClassInstanceName = "ObjectClassInstance"
  StringClassInstanceName = "StringClassInstance"

  include ::Pup::Core::Types

  attr_reader :module, :invoker

  def initialize
    @module = LLVM::Module.create("Pup")
    @build = nil
    @current_method = nil
    @block = nil
    @string_class_global = global_constant(ClassType, nil, StringClassInstanceName)
    @puts_method = build_puts_meth
    init_types
    @invoker = build_method_invoker_fn
  end

  def global_constant(type, value, name="")
    const = @module.globals.add(type, name)
    const.linkage = :internal
    const.initializer = value if value
    const.global_constant = true
    const
  end

  def global_string_constant(value, name=nil)
    str = LLVM::ConstantArray.string(value)
    name = "str#{value}" unless name
    global_constant(str, str, name)
  end

  def init_types
    object_name = global_string_constant("Object")
    object_name_ptr = object_name.gep(LLVM.Int(0), LLVM.Int(0))

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
    obj_class_global = global_constant(ClassType, main_class_instance, "Main")
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
      global_string_constant(class_name).gep(LLVM.Int(0), LLVM.Int(0)),
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

  def def_method(name)
    arg_types = [ObjectPtrType, LLVM::Int, ArgsType]
    @module.functions.add(name, arg_types, ObjectPtrType) do |fn, target, argc, argv|
      target.name = "target"
      argc.name = "argc"
      argv.name = "argv"
      last_method = @current_method
      begin
	@current_method = MethodDef.new(fn, target, argc, argv)
	yield @current_method
      ensure
	@current_method = last_method
      end
    end
  end

  class MethodDef
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
	ret_val = build.call(@module.functions["pup_main"], main_obj, LLVM.Int(0), ArgsType.null)
	b.ret(LLVM::Int32.from_i(0))
      end
    end
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
	b.call(putsf, err_msg.gep(LLVM.Int(0), LLVM.Int(0)))
	b.ret(ObjectPtrType.null)
      end
      arg = nil
      with_builder_at_end(test_str) do |b|
	arg = b.load(pup_puts.param_argv, "arg")
	gen_if_instance_of(b, arg, @module.globals[StringClassInstanceName],
			   do_puts, bad_string)
#	class_p = class_ptr_from_obj_ptr(b, arg)
#	cmp2 = b.icmp(:eq, class_p, @module.globals[StringClassInstanceName])
#	b.cond(cmp2, do_puts, bad_string)
      end

      with_builder_at_end(bad_string) do |b|
	err_msg = global_string_constant("puts() requires a String argument", "string_reqd")
	b.call(putsf, err_msg.gep(LLVM.Int(0), LLVM.Int(0)))
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

  def build_method_invoker_fn
    @module.functions.add("invoke", [ObjectPtrType, SymbolType, LLVM::Int, ArgsType], ObjectPtrType) do |invoke, target, name_sym, argc, argv|
      target.name = "target"
      name_sym.name = "name_sym"
      argc.name = "argc"
      argv.name = "argv"
      entry = invoke.basic_blocks.append("entry")
      loop_test = invoke.basic_blocks.append("loop_test")
      loop_start = invoke.basic_blocks.append("loop_start")
      do_invoke = invoke.basic_blocks.append("do_invoke")
      loop_end = invoke.basic_blocks.append("loop_end")
      raise_meth_missing = invoke.basic_blocks.append("raise_meth_missing")

      build = LLVM::Builder.create
      build.position_at_end(entry)
      curr_meth_ptr = build.alloca(MethodListEntryPtrType, "curr_meth_ptr")
      obj_elem = build.gep(target, LLVM.Int(0))
      target_class_elem = build.struct_gep(obj_elem, 0, "target_class_elem")
      clazz_ptr = build.load(target_class_elem, "class_ptr")
      clazz = build.gep(clazz_ptr, LLVM.Int(0))
      meth_list_head_elem = build.struct_gep(clazz, 3, "method_list_head_elem")
      meth_list_head_ptr = build.load(meth_list_head_elem, "method_list_head_ptr")
      build.store(meth_list_head_ptr, curr_meth_ptr)
      build.br(loop_test)

      build.position_at_end(loop_test)
      meth_entry = build.load(curr_meth_ptr, "meth_entry")
      cmp = build.is_null(meth_entry, "is_next_meth_null")
      build.cond(cmp, raise_meth_missing, loop_start)

      build.position_at_end(loop_start)
      meth_entry = build.load(curr_meth_ptr, "meth_entry")
      symbol_elem = build.gep(meth_entry, LLVM.Int(0), "symbol_elem")
      symbol_ptr = build.struct_gep(symbol_elem, 0, "symbol_ptr")
      symbol_val = build.load(symbol_ptr, "symbol")
      cmp = build.icmp(:eq, name_sym, symbol_val, "is_symbol_match")
      build.cond(cmp, do_invoke, loop_end)

      build.position_at_end(do_invoke)
      fun_elem = build.gep(meth_entry, LLVM.Int(0), "fun_element")
      methodfn_ptr = build.struct_gep(fun_elem, 1, "methodfn_ptr")
      methodfn = build.load(methodfn_ptr, "methodfn")
      ret_val = build.call(methodfn, target, argc, argv)
      build.ret(ret_val)

      build.position_at_end(loop_end)
      meth_entry = build.load(curr_meth_ptr, "meth_entry")
      next_meth_ptr = build.struct_gep(meth_entry, 2, "next_method_ptr")
      next_meth = build.load(next_meth_ptr, "next_meth")
      build.store(next_meth, curr_meth_ptr)
      build.br(loop_test)

      build.position_at_end(raise_meth_missing)
      err_msg = global_string_constant("method missing", "no_meth")
      build.call(@module.functions["puts"], err_msg.gep(LLVM.Int(0), LLVM.Int(0)))
      build.ret(ObjectPtrType.null)
      #build.unwind

      build.dispose
      @invoker = invoke
    end
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
    r = @receiver ? @receiver.codegen(ctx) : SelfExpr.new.codegen(ctx)

    argc = LLVM::Int32.from_i(@args.length)
    argv = ctx.build.array_alloca(::Pup::Core::Types::ObjectPtrType, argc, "#{@name.name}_argv")
    @args.each_with_index do |arg, i|
      a = arg.codegen(ctx)
      arg_element = ctx.build.gep(argv, [LLVM.Int(i)], "#{@name.name}_argv_#{i}")
      ctx.build.store(a, arg_element)
    end
#    argv_ptr = ctx.build.gep(argv, 0, "argv")
    sym = LLVM.Int(name.name.to_sym.to_i)
    ctx.build.call(ctx.invoker, r, sym, LLVM::Int(@args.length), argv, "#{@name.name}_ret")
  end
end

class SelfExpr
  def codegen(ctx)
    ctx.current_method.param_target
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

end
end
