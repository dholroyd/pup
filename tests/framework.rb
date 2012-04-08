require 'open4'
require 'fileutils'

class Result
  attr_accessor :stderr, :stdout, :status, :expected_exitstatus

  def initialize
    # test could override if an error is expected,
    @expected_exitstatus = 0
  end
end

class Tester
  def method_missing(name, *args, &block)
    Dir.chdir("tests") do
      raise "pup failed" unless system("../pup #{name}.pup")
      raise "llc failed" unless system("/home/dave/opt/llvm-3.0/bin/llc -load ../gclib/Release+Asserts/lib/pupgcplugin.so #{name}.bc -o #{name}.S")
      raise "as failed" unless system("as #{name}.S -o #{name}.o")
      # -rdynamic is required for the dlopen hackery used to find stack gc
      # root maps
      cmd = "gcc -rdynamic -pthread #{name}.o ../runtime.o ../exception.o ../raise.o ../string.o ../class.o ../object.o ../symtable.o ../env.o ../heap.o ../fixnum.o ../gc.o -lrt -lunwind -lunwind-x86_64 -ldl"
      raise "#{cmd.inspect} failed" unless system(cmd)
      res = Result.new
      res.status = Open4::popen4("./a.out") do |pid, stdin, stdout, stderr|
	stdin.close
	res.stderr = stderr.read
	res.stdout = stdout.read
      end
      begin
        if res.status.signaled?
	  raise "Test '#{name}' terminated with signal #{res.status.termsig}"
        end
	res.instance_exec(&block)
	if res.status.exitstatus != res.expected_exitstatus
	  $stderr.puts res.stderr
	  res.status.exitstatus.should == res.expected_exitstatus
	end
      ensure
	#FileUtils.rm ["#{name}.bc", "#{name}.S", "#{name}.o", "a.out"]
      end
    end
  end
end

def test
  Tester.new
end
