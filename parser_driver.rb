
require 'treetop'

#Treetop.load "parser"
require 'parser'

module Pup
module Parse

class << self
  def parse(file, opts={})
    parser = PupParser.new
    node = parser.parse(file, opts)
    raise parser.failure_reason unless node
    node.value
  end
end

class AST
end

class Unit < AST
  def initialize(stmts)
    @stmts = stmts
  end

  attr_reader :stmts
end

class Statements < AST
  include Enumerable

  def initialize(stmts)
    @stmts = stmts
  end

  def [](i)
    @stmts[i]
  end

  def each
    @stmts.each {|s| yield s }
  end

  def length; @stmts.length end

  def first; @stmts.first end
end

class IfStmt < AST
  attr_reader :cond, :ifbk, :elsebk

  def initialize(cond, ifbk, elsebk)
    @cond = cond
    @ifbk = ifbk
    @elsebk = elsebk
  end
end

class BeginStmt < AST
  attr_reader :beginbk, :rescuebks, :elsebk, :ensurebk

  def initialize(beginbk, rescuebks, elsebk, ensurebk)
    @beginbk = beginbk
    @rescuebks = rescuebks
    @elsebk = elsebk
    @ensurebk = ensurebk
  end
end

class RescueBlock
  attr_reader :types, :var_name, :statements

  def initialize(types, var_name, statements)
    @types, @var_name, @statements = types, var_name, statements
  end
end

class VarOrInvokeExpr < AST
  attr_accessor :name, :args, :block, :receiver

  def initialize(name, args, block, receiver=nil)
    @name = name
    @args = args
    @block = block
    @receiver = receiver
  end
end

class NameExpr < AST
  attr_reader :name
  def initialize(name)
    @name = name
  end
end

class ConstantNameExpr < AST
  attr_reader :name
  def initialize(name)
    @name = name
  end
end

class AbstractBinaryExpr < AST
  attr_reader :left, :op, :right

  def initialize(left, op, right)
    @left = left
    @op = op
    @right = right
  end
end

class AddExpr < AbstractBinaryExpr; end
class MulExpr < AbstractBinaryExpr; end
class LogicExpr < AbstractBinaryExpr; end
class EqualityExpr < AbstractBinaryExpr; end
class BitwiseExpr < AbstractBinaryExpr; end
class AssignExpr < AbstractBinaryExpr; end

class LogicNotExpr < AST
  attr_reader :subexpr
  def initialize(subexpr)
    @subexpr = subexpr
  end
end

class BitNotExpr < AST
  attr_reader :subexpr
  def initialize(subexpr)
    @subexpr = subexpr
  end
end

class StringLiteral < AST
  attr_reader :source
  def initialize(source)
    @source = source
  end

  def value
    @source[1,@source.length-2].gsub('\\"', '"')
  end
end

class IntLiteral < AST
  attr_reader :source
  def initialize(source)
    @source = source
  end

  def to_i
    @source.to_i
  end
end

class BoolLiteral
  def initialize(val)
    @val = val
  end
  def true?
    @val
  end
  def false?
    !@val
  end
end

class Block < AST
  attr_accessor :params, :statements
  def initialize(params, statements)
    @params = params
    @statements = statements
  end
end

class Param < AST
  attr_accessor :name, :init
  def initialize(name, init)
    @name = name
    @init = init
  end
end

class ClassDef < AST
  attr_reader :name, :extends, :body

  def initialize(name, extends, body)
    @name = name
    @extends = extends
    @body = body
  end
end

class MethodDef < AST
  attr_reader :name, :params, :body

  def initialize(name, params, body)
    @name = name
    @params = params
    @body = body
  end
end

class SelfExpr < AST
end

end
end
