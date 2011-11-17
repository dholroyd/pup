
module Pup
module Runtime

class RuntimeBuilder

  include ::Pup::Core::Types

  ObjectClassInstanceName = "ObjectClassInstance"
  StringClassInstanceName = "StringClassInstance"
  ExceptionClassInstanceName = "ExceptionClassInstance"

  def initialize(ctx)
    @ctx = ctx
  end

  def attach_classclass_methods
    fn = @ctx.def_method("pup_class_new") do |pup_class_new|
      @ctx.append_block do
	@ctx.with_builder_at_end do
	  theclass = pup_class_new.param_target
	  obj = @ctx.build_simple_method_invoke(theclass, "allocate")
	  @ctx.build_simple_method_invoke_argv(obj,
	                                       "initialize",
					       pup_class_new.param_argv,
					       pup_class_new.param_argc)
	  @ctx.build.ret(obj)
	end
      end
    end
    class_class_instance = @ctx.module.globals["ClassClassInstance"] || fail
    sym = LLVM.Int(:new.to_i)
    call_define_method(class_class_instance, sym, fn)

    sym = LLVM.Int(:allocate.to_i)
    pup_object_allocate = @ctx.module.functions["pup_object_allocate"]
    call_define_method(class_class_instance, sym, pup_object_allocate)

    object_class_instance = @ctx.module.globals["ObjectClassInstance"] || fail

    sym = LLVM.Int(:initialize.to_i)
    pup_object_initialize = @ctx.module.functions["pup_object_initialize"]
    call_define_method(object_class_instance, sym, pup_object_initialize)

    def_global_const(:Class, "ClassClassInstance")
    def_global_const(:Object, "ObjectClassInstance")
    def_global_const(:TrueClass, "TrueClassInstance")
    def_global_const(:FalseClass, "FalseClassInstance")
    def_global_const(:String, "StringClassInstance")
    def_global_const(:Exception, "ExceptionClassInstance")
    def_global_const(:StandardError, "StandardErrorClassInstance")
    def_global_const(:RuntimeError, "RuntimeErrorClassInstance")
  end

  def call_define_method(class_instance, sym, fn)
    @ctx.build_call.pup_define_method(class_instance, sym, fn)
  end

  def build_runtime_init
    @ctx.module.functions.add("llvm.eh.exception",
                              [],
			      LLVM::Int8.type.pointer)
    @ctx.module.functions.add("llvm.eh.selector",
                              [LLVM::Int8.type.pointer, LLVM::Int8.type.pointer],
			      LLVM::Int32, :varargs => true)
    @ctx.module.functions.add("pup_eh_personality",
                              [],
                              LLVM.Void)
    @ctx.module.functions.add("pup_handle_uncaught_exception",
                              [LLVM::Int8.type.pointer],
                              ObjectPtrType)
    @ctx.module.functions.add("pup_rethrow_uncaught_exception",
                              [LLVM::Int8.type.pointer],
                              LLVM.Void)
    @ctx.module.functions.add("pup_create_class",
                              [ClassType.pointer, ClassType.pointer, ClassType.pointer, CStrType],
			      ClassType.pointer)
    @ctx.module.functions.add("pup_define_method",
                              [ClassType.pointer, LLVM::Int, MethodPtrType],
			      LLVM.Void)
    @pup_define_method = @ctx.module.functions["pup_define_method"]
    declare_meth_impl_func("pup_object_allocate")
    declare_meth_impl_func("pup_object_initialize")
    @ctx.invoker = @ctx.module.functions.add("pup_invoke",
                              [ObjectPtrType, SymbolType, LLVM::Int, ArgsType],
			      ObjectPtrType)
    @ctx.module.functions.add("extract_exception_obj",
                              [LLVM::Int8.type.pointer],
			      ObjectPtrType)
    @ctx.module.functions.add("pup_string_create",
                              [CStrType],
			      ObjectPtrType)
    @ctx.module.functions.add("pup_const_get_required",
                              [ClassType.pointer, LLVM::Int],
			      ObjectPtrType)
    @ctx.module.functions.add("pup_const_set",
                              [ClassType.pointer, LLVM::Int, ObjectPtrType],
			      LLVM.Void)
    @ctx.module.functions.add("pup_iv_set",
                              [ObjectPtrType, LLVM::Int, ObjectPtrType],
			      LLVM.Void)
    @ctx.module.functions.add("pup_iv_get",
                              [ObjectPtrType, LLVM::Int],
			      ObjectPtrType)
    @ctx.module.functions.add("pup_class_context_from",
                              [ObjectPtrType],
			      ClassType.pointer)
    @ctx.module.functions.add("pup_is_descendant_or_same",
                              [ObjectPtrType, ObjectPtrType],
			      LLVM::Int1)
    @ctx.module.functions.add("pup_runtime_init", [], LLVM.Void) do |fn|
      b = fn.basic_blocks.append
      @ctx.with_builder_at_end(b) do
	attach_classclass_methods
	@ctx.build.ret_void
      end
    end
    @ctx.module.functions.add("pup_create_instance", [ClassType.pointer, LLVM::Int, ArgsType], ObjectPtrType) do |fn, clazz, argc, argv|
      clazz.name = "clazz"
      argc.name = "argc"
      argv.name = "argv"
      b = fn.basic_blocks.append
      @ctx.with_builder_at_end(b) do
	o = @ctx.build.bit_cast(clazz, ::Pup::Core::Types::ObjectPtrType, "clazz_asobj")
	res = @ctx.build_simple_method_invoke_argv(o, "new", argv, argc)
	@ctx.build.ret res
      end
    end
    @ctx.module.functions.add("pup_exception_message_set", [ObjectPtrType, ObjectPtrType], LLVM.Void) do |fn, target, value|
      target.name = "target"
      value.name = "value"
      b = fn.basic_blocks.append
      @ctx.with_builder_at_end(b) do
	@ctx.build_call.pup_iv_set(target, LLVM.Int(:@message.to_i), value)
	@ctx.build.ret_void
      end
    end
    @ctx.module.functions.add("pup_exception_message_get", [ObjectPtrType], ObjectPtrType) do |fn, target|
      target.name = "target"
      b = fn.basic_blocks.append
      @ctx.with_builder_at_end(b) do
	ret = @ctx.build_call.pup_iv_get(target, LLVM.Int(:@message.to_i))
	@ctx.build.ret ret
      end
    end
  end


  def init_types
    object_name_ptr = @ctx.global_string_constant("Object")

    @ctx.module.types.add("Object", ObjectType)
    @ctx.module.types.add("Class", ClassType)
    @ctx.module.types.add("String", StringObjectType)
    @ctx.module.types.add("Method", MethodType)
    @ctx.module.types.add("AttributeListEntry", AttributeListEntryType)
    @ctx.module.types.add("MethodListEntry", MethodListEntryType)

    class_class_fwd_decl = @ctx.module.globals.add(ClassType, "ClassClassInstance")
    class_class_fwd_decl.linkage = :external
    
    object_class_instance = @ctx.const_struct(
      # object header,
      @ctx.const_struct(
	# class,
        @ctx.module.globals["ClassClassInstance"],
	# attribute list head
	AttributeListEntryType.pointer.null_pointer
      ),
      # superclass (none, for 'Object')
      ClassType.pointer.null_pointer,
      # class name,
      object_name_ptr,
      # method list head,
      @ctx.build_meth_list_entry(:to_s, @class_to_s_method),
      # lexical scope
      ClassType.pointer.null
    )
    obj_class_global = @ctx.global_constant(ClassType, object_class_instance, ObjectClassInstanceName)
    class_class_instance = @ctx.build_class_instance("Class", obj_class_global)
    class_class_fwd_decl.initializer = class_class_instance

    string_class_instance = @ctx.build_class_instance("String", obj_class_global)
    @string_class_global = @ctx.global_constant(ClassType, nil, StringClassInstanceName)
    @string_class_global.linkage = :external
    @string_class_global.initializer = string_class_instance

    exception_class_instance = @ctx.build_class_instance(
      "Exception", obj_class_global,
      @ctx.build_meth_list_entry(:to_s, @exception_to_s,
	@ctx.build_meth_list_entry(:initialize, @exception_initialize,
	  @ctx.build_meth_list_entry(:message, @exception_message)))
    )

    @exception_class_global = @ctx.global_constant(ClassType, nil, ExceptionClassInstanceName)
    @exception_class_global.linkage = :external
    @exception_class_global.initializer = exception_class_instance

    standarderror_class_instance = @ctx.build_class_instance("StandardError", @exception_class_global)
    standarderror_class_global = @ctx.global_constant(ClassType, nil, "StandardErrorClassInstance")
    standarderror_class_global.linkage = :external
    standarderror_class_global.initializer = standarderror_class_instance

    runtimeerror_class_instance = @ctx.build_class_instance("RuntimeError", @exception_class_global)
    runtimeerror_class_global = @ctx.global_constant(ClassType, nil, "RuntimeErrorClassInstance")
    runtimeerror_class_global.linkage = :external
    runtimeerror_class_global.initializer = runtimeerror_class_instance


    true_class_instance = @ctx.build_class_instance("TrueClass", obj_class_global)
    true_class_global = @ctx.global_constant(ClassType, true_class_instance, "TrueClassInstance")
    false_class_instance = @ctx.build_class_instance("FalseClass", obj_class_global)
    false_class_global = @ctx.global_constant(ClassType, false_class_instance, "FalseClassInstance")

    true_obj_instance = @ctx.const_struct(
      true_class_global,
      AttributeListEntryType.pointer.null
    )
    true_global = @ctx.global_constant(ObjectType, true_obj_instance, "TrueObjInstance")
    false_obj_instance = @ctx.const_struct(
      false_class_global,
      AttributeListEntryType.pointer.null
    )
    false_global = @ctx.global_constant(ObjectType, false_obj_instance, "FalseObjInstance")

    main_class_instance = @ctx.build_class_instance("Main", obj_class_global, @ctx.build_meth_list_entry(:puts, @puts_method, @ctx.build_meth_list_entry(:raise, @raise_method)))
    @ctx.global_constant(ClassType, main_class_instance, "Main")
  end

  def build_puts_meth
    putsf = @ctx.module.functions.add("puts", [CStrType], LLVM::Int32)
    # TODO: move!
    @ctx.module.functions.add("abort", [], LLVM.Void)
    @ctx.module.functions.add("printf", [CStrType], LLVM::Int, :varargs => true)

    @puts_method = declare_meth_impl_func("pup_puts")
    @raise_method = declare_meth_impl_func("pup_object_raise")
    @class_to_s_method = declare_meth_impl_func("pup_class_to_s")
    @exception_initialize = declare_meth_impl_func("pup_exception_initialize")
    @exception_to_s = declare_meth_impl_func("pup_exception_to_s")
    @exception_message = declare_meth_impl_func("pup_exception_message")
  end

  private

  def declare_meth_impl_func(name)
    arg_types = [ObjectPtrType, LLVM::Int, ArgsType]
    @ctx.module.functions.add(name, arg_types, ObjectPtrType)
  end

  def def_global_const(sym, global_name)
    global = @ctx.module.globals[global_name]
    raise "No global #{global_name}" unless global
    @ctx.build_call.pup_const_set(@ctx.module.globals["Main"], LLVM.Int(sym.to_sym.to_i), global.bit_cast(ObjectPtrType))
  end

end

end  # module Runtime
end  # module Pup
