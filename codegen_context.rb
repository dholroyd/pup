
module Pup
module Parse

class CodegenContext

  ObjectClassInstanceName = "ObjectClassInstance"
  StringClassInstanceName = "StringClassInstance"
  ExceptionClassInstanceName = "ExceptionClassInstance"

  include ::Pup::Core::Types

  attr_accessor :invoker
  attr_reader :module, :runtime_builder, :string_class_global, :landingpad
  # reference to the LLVM Value for the current exception being handled, if any
  attr_reader :excep

  def self_ref
    raise "reference to 'self' is not yet defined" unless @self_ref
    @self_ref
  end

  def next_serial
    @serial += 1
  end

  def initialize
    @module = LLVM::Module.create("Pup")
    @build = nil
    @self_ref = nil
    @current_method = nil
    @block = nil
    @serial = 0
    @landingpad = nil
    @excep = nil
    @string_class_global = global_constant(ClassType, nil, StringClassInstanceName)
    @string_class_global.linkage = :external
    @exception_class_global = global_constant(ClassType, nil, ExceptionClassInstanceName)
    @exception_class_global.linkage = :external
    build_puts_meth
    init_types

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
    exception_class_instance = build_class_instance("Exception", obj_class_global)
    @exception_class_global.initializer = exception_class_instance


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

    main_class_instance = build_class_instance("Main", obj_class_global, build_meth_list_entry(:puts, @puts_method, build_meth_list_entry(:raise, @raise_method)))
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
  def def_method_uniquely(basename)
    def_method("meth_#{@meth_seq += 1}_#{basename}") do |meth|
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
      catch_block = mainfn.basic_blocks.append("catch")
      exit_block = mainfn.basic_blocks.append("exit")
      with_builder_at_end(entry) do |b|
	main_obj = b.alloca(ObjectType, "main_obj")
	main_obj_class = b.struct_gep(main_obj, 0, "main_obj_class")
	b.store(@module.globals["Main"], main_obj_class)
	attr_list_head = b.struct_gep(main_obj, 1, "attr_list_head")
	b.store(AttributeListEntryType.pointer.null, attr_list_head)

	ret_val = build.call(@module.functions["pup_runtime_init"])

	ret_val = build.invoke(@module.functions["pup_main"], [main_obj, LLVM.Int(0), ArgsType.null], exit_block, catch_block)
      end
      with_builder_at_end(catch_block) do |b|
	excep = b.call(@module.functions["llvm.eh.exception"], "excep")
	sel = b.call(@module.functions["llvm.eh.selector"],
	             excep,
	             @module.functions["pup_eh_personality"].bit_cast(LLVM::Int8.type.pointer),
	             @module.globals["ExceptionClassInstance"], "sel")
	b.call(@module.functions["pup_handle_uncaught_exception"], excep)
	b.br(exit_block)
      end
      with_builder_at_end(exit_block) do |b|
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

  def eh_begin(landingpad)
    last_landingpad = @landingpad
    @landingpad = landingpad
    yield 
  ensure
    @landingpad = last_landingpad
  end

  def eh_handle(excep)
    last_excep = @excep
    @excep = excep
    yield
  ensure
    @excep = last_excep
  end

  def eh_active?
    !@landingpad.nil?
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
    @module.functions.add("printf", [CStrType], LLVM::Int, :varargs => true)

    arg_types = [ObjectPtrType, LLVM::Int, ArgsType]
    @puts_method = @module.functions.add("pup_puts", arg_types, ObjectPtrType)
    @raise_method = @module.functions.add("pup_object_raise", arg_types, ObjectPtrType)
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

  def build_meth_list_entry(name, fn_ptr, next_entry = nil)
    entry = const_struct(
      LLVM.Int(name.to_sym.to_i),
      fn_ptr,
      next_entry || MethodListEntryPtrType.null_pointer
    )
    global_constant(MethodListEntryType, entry, "meth_entry_#{name}")
  end
end

end  # module Parse
end  # module Pup
