require 'framework'
require 'rspec/expectations'

include RSpec::Matchers

# in invocation test.if_true runs the code in "if_true.pup" etc.

test.if_true do
  stdout.should match /success/
end
test.if_false do
  stdout.should match /success/
end
test.simple_instance_method do
  stdout.should match /success/
end
test.default_rescue_block do
  stdout.should match /success/
end
test.raise_string do
  stdout.should match /success/
end
test.globals do
  stdout.split(/\s/).should == %w{Object Class String TrueClass FalseClass Exception}
end
