
module Pup
module Runtime

class RuntimeBuilder

  include ::Pup::Core::Types

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
			      LLVM::Int32, true)
    @ctx.module.functions.add("pup_eh_personality",
                              [],
                              LLVM.Void)
    @ctx.module.functions.add("pup_handle_uncaught_exception",
                              [LLVM::Int8.type.pointer],
                              ObjectPtrType)
    @ctx.module.functions.add("pup_create_class",
                              [ClassType.pointer, ClassType.pointer, CStrType],
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
    @ctx.module.functions.add("pup_runtime_init", [], LLVM.Void) do |fn|
      b = fn.basic_blocks.append
      @ctx.with_builder_at_end(b) do
	attach_classclass_methods
	@ctx.build.ret_void
      end
    end
  end

end

end
end
