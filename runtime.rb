
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

  def call_define_method(class_instance, sym, fn)
    @ctx.build_call.pup_define_method(class_instance, sym, fn)
  end

  def build_runtime_init
    # external function declarations,
    [
      ["llvm.eh.exception",
	[],
	LLVM::Int8.type.pointer],
      ["llvm.eh.selector",
	[LLVM::Int8.type.pointer, LLVM::Int8.type.pointer],
	LLVM::Int32, {:varargs => true}],
      ["pup_eh_personality",
	[],
	LLVM.Void],
      ["pup_runtime_env_create",
	[],
	EnvPtrType],
      ["pup_handle_uncaught_exception",
	[EnvPtrType, LLVM::Int8.type.pointer],
	ObjectPtrType],
      ["pup_rethrow_uncaught_exception",
	[LLVM::Int8.type.pointer],
	LLVM.Void],
      ["pup_create_class",
	[EnvPtrType, ClassType.pointer, ClassType.pointer, ClassType.pointer, CStrType],
	ClassType.pointer],
      ["pup_define_method",
	[ClassType.pointer, LLVM::Int, MethodPtrType],
	LLVM.Void],
      ["pup_invoke",
	[EnvPtrType, ObjectPtrType, SymbolType, LLVM::Int, ArgsType],
	ObjectPtrType],
      ["extract_exception_obj",
	[LLVM::Int8.type.pointer],
	ObjectPtrType],
      ["pup_string_new_cstr",
	[EnvPtrType, CStrType],
	ObjectPtrType],
      ["pup_const_get_required",
	[EnvPtrType, ClassType.pointer, LLVM::Int],
	ObjectPtrType],
      ["pup_const_set",
	[EnvPtrType, ClassType.pointer, LLVM::Int, ObjectPtrType],
	LLVM.Void],
      ["pup_iv_set",
	[EnvPtrType, ObjectPtrType, LLVM::Int, ObjectPtrType],
	LLVM.Void],
      ["pup_iv_get",
	[ObjectPtrType, LLVM::Int],
	ObjectPtrType],
      ["pup_class_context_from",
	[EnvPtrType, ObjectPtrType],
	ClassType.pointer],
      ["pup_is_descendant_or_same",
	[ObjectPtrType, ObjectPtrType],
	LLVM::Int1],
      ["pup_env_get_trueinstance",
	[EnvPtrType],
	ObjectPtrType],
      ["pup_env_get_falseinstance",
	[EnvPtrType],
	ObjectPtrType],
      ["pup_env_get_classobject",
	[EnvPtrType],
	ClassType.pointer],
      ["pup_env_get_classclass",
	[EnvPtrType],
	ClassType.pointer],
      ["pup_env_get_classexception",
	[EnvPtrType],
	ObjectPtrType],
      ["pup_create_object",
	[EnvPtrType, ClassType.pointer],
	ObjectPtrType],
      ["pup_env_str_to_sym",
	[EnvPtrType, CStrType],
	LLVM::Int],
      ["pup_fixnum_create",
	[EnvPtrType, LLVM::Int],
	ObjectPtrType]
    ].each do |args|
      @ctx.module.functions.add(*args)
    end
  end

  def init_types
    object_name_ptr = @ctx.global_string_constant("Object")
  end

  private

  def declare_meth_impl_func(name)
    arg_types = [EnvPtrType, ObjectPtrType, LLVM::Int, ArgsType]
    @ctx.module.functions.add(name, arg_types, ObjectPtrType)
  end

  def def_global_const(sym, global_name)
    global = @ctx.module.globals[global_name]
    raise "No global #{global_name}" unless global
    @ctx.build_call.pup_const_set(@ctx.global.Main, @ctx.mk_sym(sym), global.bit_cast(ObjectPtrType))
  end

end

end  # module Runtime
end  # module Pup
