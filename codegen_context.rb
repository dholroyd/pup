
module Pup
module Parse

class CodegenContext

  include ::Pup::Core::Types

  attr_reader :module, :runtime_builder, :string_class_global, :landingpad, :block
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

    @call_sugar = CallSugar.new(self)
    @global_sugar = GlobalSugar.new(self)
    @runtime_builder = ::Pup::Runtime::RuntimeBuilder.new(self)
    @runtime_builder.init_types
    env = EnvPtrType.null
    @current_method = Struct.new("FakeMethod", :env).new(env)
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
    arg_types = [EnvPtrType, ObjectPtrType, LLVM::Int, ArgsType]
    @module.functions.add(name, arg_types, ObjectPtrType) do |fn, env, target, argc, argv|
      env.name = "env"
      target.name = "target"
      argc.name = "argc"
      argv.name = "argv"
      last_method = @current_method
      begin
	@current_method = MethodRef.new(fn, env, target, argc, argv)
	using_self(target) do
	  yield @current_method
        end
      ensure
	@current_method = last_method
      end
    end
  end

  class MethodRef
    attr_reader :function, :env, :param_target, :param_argc, :param_argv
    def initialize(function, env, param_target, param_argc, param_argv)
      @entry_block_builder = nil
      @function = function
      @env = env
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
        env = build_call.pup_runtime_env_create()
	main_class = build_call.pup_env_get_classobject(env)
	main_obj = build_call.pup_create_object(env, main_class)

	@current_method = Struct::FakeMethod.new(env)

	ret_val = build.invoke(@module.functions["pup_main"], [env, main_obj, LLVM.Int(0), ArgsType.null], exit_block, catch_block)
      end
      with_builder_at_end(catch_block) do |b|
	excep = b.call(@module.functions["llvm.eh.exception"], "excep")
	sel = b.call(@module.functions["llvm.eh.selector"],
	             excep,
	             @module.functions["pup_eh_personality"].bit_cast(LLVM::Int8.type.pointer),
	             build_call.pup_env_get_classexception(current_method.env), "sel")
	build_call.pup_handle_uncaught_exception(current_method.env, excep)
	b.br(exit_block)
      end
      with_builder_at_end(exit_block) do |b|
	b.ret(LLVM::Int32.from_i(0))
      end
    end
  end

  def build_method_invocation(receiver, name, *args)
    arg_count = args.length
    argc = LLVM::Int32.from_i(arg_count)
    if args.empty?
      argv = ::Pup::Core::Types::ObjectPtrType.pointer.null
    else
      argv = build.array_alloca(::Pup::Core::Types::ObjectPtrType, argc, "#{name}_argv")
      args.each_with_index do |arg, i|
	arg_element = build.gep(argv, [LLVM.Int(i)], "#{name}_argv_#{i}")
	build.store(arg, arg_element)
      end
    end
    sym = mk_sym(name)
    if eh_active?
      # block following invocation; continue here if no exception raised
      bkcontinue = current_method.function.basic_blocks.append("invoke_#{name}_continue")
      res = build.invoke(@module.functions["pup_invoke"],
                       [current_method.env, receiver, sym, LLVM::Int(arg_count), argv],
                       bkcontinue, landingpad,
		       "#{name}_ret")
      build.position_at_end(bkcontinue)
      res
    else
      build_call.pup_invoke(current_method.env,
                     receiver, sym, LLVM::Int(arg_count), argv,
		     "#{name}_ret")
    end
  end

  def build_call
    @call_sugar
  end

  def global
    @global_sugar
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

  # Makes an LLVM Int from name.to_sym.to_i
  def mk_sym(name)
    ret = build_call.pup_env_str_to_sym(current_method.env, global_string_constant(name))
    ret.name = "sym_#{name}"
    ret
  end
end

class CallSugar
  def initialize(ctx)
    @ctx = ctx
  end

  def method_missing(name, *args)
    fun = @ctx.module.functions[name]
    raise "no module function #{name.inspect} currently defined" unless fun
    @ctx.build.call(fun, *args);
  end
end

class GlobalSugar
  def initialize(ctx)
    @ctx = ctx
  end

  def method_missing(name, *args)
    global = @ctx.module.globals[name]
    raise "no module global #{name.inspect} currently defined" unless global
    global
  end
end

end  # module Parse
end  # module Pup
