
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
  end

  def call_define_method(class_instance, sym, fn)
    @ctx.build.call(@pup_define_method, class_instance, sym, fn)
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
    @ctx.module.functions.add("pup_create_class",
                              [ClassType.pointer, ClassType.pointer, ClassType.pointer, CStrType],
			      ClassType.pointer)
    @ctx.module.functions.add("pup_define_method",
                              [ClassType.pointer, LLVM::Int, MethodPtrType],
			      LLVM.Void)
    @pup_define_method = @ctx.module.functions["pup_define_method"]
    @ctx.module.functions.add("pup_object_allocate",
                              [ObjectPtrType, LLVM::Int, ArgsType],
			      ObjectPtrType)
    @ctx.module.functions.add("pup_object_initialize",
                              [ObjectPtrType, LLVM::Int, ArgsType],
			      ObjectPtrType)
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
    @ctx.module.functions.add("pup_class_context_from",
                              [ObjectPtrType],
			      ClassType.pointer)
    @ctx.module.functions.add("pup_runtime_init", [], LLVM.Void) do |fn|
      b = fn.basic_blocks.append
      @ctx.with_builder_at_end(b) do
	attach_classclass_methods
	@ctx.build.ret_void
      end
    end
  end


  def init_types
    @string_class_global = @ctx.global_constant(ClassType, nil, StringClassInstanceName)
    @string_class_global.linkage = :external
    @exception_class_global = @ctx.global_constant(ClassType, nil, ExceptionClassInstanceName)
    @exception_class_global.linkage = :external

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
    @string_class_global.initializer = string_class_instance
    exception_class_instance = @ctx.build_class_instance("Exception", obj_class_global)
    @exception_class_global.initializer = exception_class_instance


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

    arg_types = [ObjectPtrType, LLVM::Int, ArgsType]
    @puts_method = @ctx.module.functions.add("pup_puts", arg_types, ObjectPtrType)
    @raise_method = @ctx.module.functions.add("pup_object_raise", arg_types, ObjectPtrType)
    @class_to_s_method = @ctx.module.functions.add("pup_class_to_s", arg_types, ObjectPtrType)
  end

  private

  def def_global_const(sym, global_name)
    global = @ctx.module.globals[global_name]
    raise "No global #{global_name}" unless global
    @ctx.build.call(@ctx.module.functions["pup_const_set"], @ctx.module.globals["Main"], LLVM.Int(sym.to_sym.to_i), global.bit_cast(ObjectPtrType))
  end

end

end  # module Runtime
end  # module Pup
