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
      raise "llvmld failed" unless system("llvm-ld-2.9 -disable-opt -disable-inlining -native #{name}.bc ../runtime.bc ../exception.bc ../raise.bc ../string.bc ../class.bc")
      res = Result.new
      res.status = Open4::popen4("./a.out") do |pid, stdin, stdout, stderr|
	stdin.close
	res.stderr = stderr.read
	res.stdout = stdout.read
      end
      begin
	res.instance_exec(&block)
	if res.status.exitstatus != res.expected_exitstatus
	  $stderr.puts res.stderr
	  res.status.exitstatus.should == res.expected_exitstatus
	end
      ensure
	FileUtils.rm ["#{name}.bc", "a.out", "a.out.bc"]
      end
    end
  end
end

def test
  Tester.new
end
