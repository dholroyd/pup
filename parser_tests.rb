require 'test/unit'
require 'parser_driver'
require 'pp'


class ParserTest < Test::Unit::TestCase

  include Pup::Parse

  # debug dump
  def dd(m, i="")
    case m
      when Statements
	m.map{|s| i+dd(s, i) }.join("\n")
      when InvokeExpr
	"(" + (m.receiver ? "ON " + dd(m.receiver)+" " : "") +
	"INVOKE " +
          "#{dd(m.name, i)}" +
	  (m.args ? " "+m.args.map{|a|dd(a, i)}.join(", ") : "") +
	  (m.block ? dd(m.block, i) : "") + ")"
      when Block
	"{" + (m.params ? "|"+m.params.map{|p|dd(p,i)}.join(", ")+"|" : "") +
	  (m.statements ? "\n"+dd(m.statements, i+"  ")+"\n" : "") + i+"}"
      when NameExpr
	m.name
      when Param
	dd(m.name, i) + (m.init ? "="+dd(m.init, i) : "")
      when IfStmt
	"(IF #{dd(m.cond)}\n#{dd(m.ifbk,i+"  ")}#{m.elsebk ? "\n"+i+"ELSE\n"+dd(m.elsebk, i+"  ") : ""}\n)"
      when ClassDef
	"class #{dd(m.name, i)}" + (m.extends ? " < " + dd(m.extends, i) : "") + "\n" +
	  dd(m.body, i+"  ") +
	  i+"end"
      when MethodDef
	"def #{dd(m.name, i)}" + (m.params ? "(" + m.params.map{|p|dd(p,i)}.join(", ")+")" : "") + "\n" +
	  dd(m.body, i+"  ") +
	  i+"end"
      when StringLiteral
	m.source
      else
	raise "unhandled #{m.class} - #{m.inspect} #{m.respond_to?(:name) ? "(#{m.name})" : ""}"
    end
  end

  def pdd(m); puts dd(m) end

  def pz(text)
    ::Pup::Parse.parse(text)
  end

  def assert_length(expected, obj)
    assert_equal(expected, obj.length, "Expected length #{expected}, but was #{obj.length} for #{obj.inspect}")
  end

  def test_class_def_basic
    t = pz <<-E
      class Foo
      end
    E
  end

  def test_class_def_extends
    t = pz <<-E
      class Foo < bar
      end
    E
  end

  def test_meth_def_basic
    t = pz <<-E
      def f
      end
    E
  end

  def test_meth_def_params
    t = pz <<-E
      def f(a, b="")
      end
    E
  end

  def test_statements
    r = pz <<-E
      foo_stmt
      bar_stmt
    E
    assert_length(2, r)
  end

  def test_if
    pz <<-E
      if asia
	basia
      end
    E
  end

  def test_ifelse
    t = pz <<-E
      if asia
	basia
      else
	foo
	bar
      end
    E
    ifstmt = t.first
    assert_equal("asia", ifstmt.cond.name.name)
    assert_equal("basia", ifstmt.ifbk.first.name.name)
    assert_equal("foo", ifstmt.elsebk.first.name.name)
  end

  def test_meth_one_arg
    t = pz "foo(bar)"
  end

  def test_meth_many_arg
    t = pz "foo(blat,bar)"
  end

  def test_meth_noparen
    t = pz "puts \"hello\", \"world\""
  end

  def test_methlookup
    t = pz "foo().bar"
  end

  def test_method_called_class
    t = pz "foo.class"
  end

  def test_block
    t = pz "foo{ |bar, blat| foobar }"
    block_params = t[0].block.params
    assert_length(2, block_params)
  end

  def test_multimethlookup
    t = pz "foo.bar{a}.blat"
  end

  def test_no_assign_fncall
    assert_raise(RuntimeError) {
      pz "foo() = bar"
    }
  end

  def exp(source)
    pz(source)[0]
  end

  def test_add
    e = exp "1 + 2"
    assert_equal(1, e.left.to_i)
    assert_equal(2, e.right.to_i)
    assert_equal(:+, e.op)
  end

  def test_bin_ops
    %w{+ - * / && || & | != ==}.each do |op|
      e = exp "a #{op} 2"
      assert_equal("a", e.left.name.name)
      assert_equal(2, e.right.to_i)
      assert_equal(op.to_sym, e.op)
    end
  end

  def test_assign_ops
    %w{= %= ^= &= &&= |= ||= *= /= -= <<= >>=}.each do |op|
      e = exp "a #{op} 2"
      assert_equal("a", e.left.name)
      assert_equal(2, e.right.to_i)
      assert_equal(op.to_sym, e.op)
    end
  end
end
