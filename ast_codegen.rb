
# extends AST classes with codegen() methods which build LLVM IR

require 'runtime'

module Pup
module Parse

class CodegenContext

  ObjectClassInstanceName = "ObjectClassInstance"
  StringClassInstanceName = "StringClassInstance"

  include ::Pup::Core::Types

  attr_accessor :invoker
  attr_reader :module, :self_ref

  def initialize
    @module = LLVM::Module.create("Pup")
    @build = nil
    @self_ref = nil
    @current_method = nil
    @block = nil
    @string_class_global = global_constant(ClassType, nil, StringClassInstanceName)
    @puts_method = build_puts_meth
    init_types
    #@invoker = build_method_invoker_fn

    rtb = ::Pup::Runtime::RuntimeBuilder.new(self)
    rtb.build_runtime_init
  end

  def global_constant(type, value, name="")
    const = @module.globals.add(type, name)
    const.linkage = :internal
    const.initializer = value if value
#    const.global_constant = true
    const
  end

  def global_string_constant(value, name=nil)
    str = LLVM::ConstantArray.string(value)
    name = "str#{value}" unless name
    global_constant(str, str, name).bit_cast(CStrType)
  end

  def init_types
    object_name_ptr = global_string_constant("Object")
    #object_name_ptr = object_name.gep(LLVM.Int(0), LLVM.Int(0))

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
      global_string_constant(class_name), #.gep(LLVM.Int(0), LLVM.Int(0)),
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
    old_self = @self_ref
    begin
      @self_ref = new_self
      yield
    ensure
      @self_ref = old_self
    end
  end

  def def_method(name)
    arg_types = [ObjectPtrType, LLVM::Int, ArgsType]
    @module.functions.add(name, arg_types, ObjectPtrType) do |fn, target, argc, argv|
      target.name = "target"
      argc.name = "argc"
      argv.name = "argv"
      last_method = @current_method
      begin
	@current_method = MethodRef.new(fn, target, argc, argv)
	using_self(target) do
	  yield @current_method
        end
      ensure
	@current_method = last_method
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

    arg_types = [ObjectPtrType, LLVM::Int, ArgsType]
    @module.functions.add("pup_puts", arg_types, ObjectPtrType)
  end

  def attach_classclass_methods(class_class_instance)
    def_method("pup_class_new") do |pup_class_new|
      body = append_block do
	with_builder_at_end do
	  theclass = pup_class_new.param_target
	  obj = build_simple_method_invoke(theclass, "allocate")
	  build_simple_method_invoke_argv(obj, "initialize", pup_class_new.param_argv, pup_class_new.param_argc)
	  build.ret(obj)
	end
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
#    argv_ptr = ctx.build.gep(argv, 0, "argv")
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
    class_name = name.name
    classref = ctx.current_method.get_or_create_local(class_name)
    classdef = nil
    class_name_ref = ctx.global_string_constant(class_name)
    superclass_ref = find_superclass(ctx)
    ctx.eval_build do
      classdef = call(ctx.module.functions["pup_create_class"],
                      ctx.module.globals["ClassClassInstance"],
		      superclass_ref,
		      class_name_ref,
                      "class_#{class_name}")
      classref = bit_cast(classref, ClassType.pointer.pointer, "as_obj_#{class_name}")
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
  include ::Pup::Core::Types

  def codegen(ctx)
    mangled_name = "pup_method__#{name.name}_#{next_serial}"
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
    hopefully_a_class = class_context(ctx)
    ctx.build.call(ctx.module.functions["pup_define_method"],
                   hopefully_a_class,
                   LLVM.Int(name.name.to_sym.to_i),
                   fn)
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

  def next_serial
    @serial = @serial ? @serial+1 : 0
  end
end

end
end
