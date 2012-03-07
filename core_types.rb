
require 'llvm/core'
require 'llvm/analysis'


module Pup
module Core
module Types

CStrType = LLVM.Pointer(LLVM::Int8)

SymbolType = LLVM::Int

ObjectType = LLVM::Struct("PupObject")
ObjectPtrType = ObjectType.pointer

ArgsType = ObjectPtrType.pointer

EnvType = LLVM.Struct()
EnvPtrType = EnvType.pointer

MethodType = LLVM.Function([EnvPtrType, ObjectPtrType, LLVM::Int, ArgsType], ObjectPtrType)

MethodPtrType = MethodType.pointer
MethodListEntryType = LLVM::Struct("MethodListEntry")
MethodListEntryType.element_types = [
  SymbolType,  # method name
  MethodPtrType, # method impl
  MethodListEntryType.pointer  # next list entry
]
MethodListEntryPtrType = MethodListEntryType.pointer

AttributeListEntryType = LLVM::Struct("AttributeListEntry")
AttributeListEntryType.element_types = [
  SymbolType,  # attribute name
  ObjectPtrType, # attribute value
  AttributeListEntryType.pointer  # next list entry
]

ClassType = LLVM::Struct("PupClass")
ClassType.element_types = [
  ObjectType,  # Since the 'Class' class is a kind of Object
  ClassType.pointer,  # superclass
  CStrType,     # class name
  MethodListEntryType.pointer,  # head of the method linked list
  ClassType.pointer   # the lexical scope of the class definition
]

ObjectType.element_types = [
  ClassType.pointer,   # the class of this object
  AttributeListEntryType.pointer  # head of the method linked list
]

# Instances require that their ClassType be pointed at the "Integer"
# ClassType instance created elsewhere
IntObjectType = LLVM.Struct(
  ObjectType,
  LLVM::Int
)

StringObjectType = LLVM.Struct(
  ObjectType,
  CStrType
)


end
end
end
