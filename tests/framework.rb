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
      cmd = "gcc -rdynamic -pthread #{name}.o ../runtime.o ../exception.o ../raise.o ../string.o ../class.o ../object.o ../symtable.o ../env.o ../heap.o ../fixnum.o ../gc.o ../gc/refqueue.o -lrt -lunwind -lunwind-x86_64 -ldl"
      raise "#{cmd.inspect} failed" unless system(cmd)
      res = Result.new
      opts = args[0]
      vmlimit = opts ? opts[:vmlimit] : nil
      if vmlimit
        vmlimit = vmlimit.to_i * 1024  # convert Mb to Kb
        # when limiting VM size, allow core dumps, so that we can debug if
        # the test fails,
        cmd = "bash -c 'ulimit -c #{1024*64} -v #{vmlimit} && ./a.out'"
      else
        cmd = "./a.out"
      end
      res.status = Open4::popen4(cmd) do |pid, stdin, stdout, stderr|
	stdin.close
	res.stderr = stderr.read
	res.stdout = stdout.read
      end
      success = false
      begin
        if res.status.signaled?
	  raise "Test '#{name}' terminated with signal #{res.status.termsig}"
        end
        if res.status.coredump?
	  raise "Test '#{name}' dumped core"
        end
	res.instance_exec(&block)
	if res.status.exitstatus != res.expected_exitstatus
	  res.status.exitstatus.should == res.expected_exitstatus
	end
        success = true
      rescue
	$stderr.puts "== stderr start =="
	$stderr.puts res.stderr
	$stderr.puts "== stderr end =="
        raise
      ensure
	FileUtils.rm ["#{name}.bc", "#{name}.S", "#{name}.o", "a.out"]
      end
    end
  end
end

def test
  Tester.new
end
