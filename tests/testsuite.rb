require 'framework'
require 'rspec/expectations'

include RSpec::Matchers

# an invocation of "test.if_true" runs the code in "if_true.pup" etc.

test.gc(:vmlimit=>256) do
  stdout.should match /success/
end
test.while do
  stdout.should match /^\s*success\s+success\s+success\s*$/
end
test.integer do
  stdout.should match /success/
end
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
test.rescue_types do
  stdout.should match /success/
end
