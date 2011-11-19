
require 'llvm/core'
require 'llvm/analysis'


module Pup
module Core
module Types

CStrType = LLVM.Pointer(LLVM::Int8)

SymbolType = LLVM::Int

obj_ty = LLVM::Type.opaque  # forward declaration, refined later
ObjectPtrType = LLVM::Type.pointer(obj_ty)

ArgsType = ObjectPtrType.pointer

EnvType = LLVM.Struct()
EnvPtrType = EnvType.pointer

MethodType = LLVM.Function([EnvPtrType, ObjectPtrType, LLVM::Int, ArgsType], ObjectPtrType)

MethodPtrType = MethodType.pointer
MethodListEntryType = LLVM::Type.rec do |thistype|
  LLVM.Struct(
    SymbolType,  # method name
    MethodPtrType, # method impl
    thistype.pointer  # next list entry
  )
end
MethodListEntryPtrType = MethodListEntryType.pointer

AttributeListEntryType = LLVM::Type.rec do |thistype|
  LLVM.Struct(
    SymbolType,  # attribute name
    ObjectPtrType, # attribute value
    thistype.pointer  # next list entry
  )
end

ClassType = LLVM::Type.rec do |thistype|
  LLVM.Struct(
    obj_ty,  # Since the 'Class' class is a kind of Object
    thistype.pointer,  # superclass
    CStrType,     # class name
    MethodListEntryType.pointer,  # head of the method linked list
    thistype.pointer   # the lexical scope of the class definition
  )
end

ObjectType = LLVM.Struct(
  ClassType.pointer,   # the class of this object
  AttributeListEntryType.pointer  # head of the method linked list
)

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

obj_ty.refine(ObjectType)

end
end
end
