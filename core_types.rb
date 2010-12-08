
require 'llvm/core'
require 'llvm/analysis'


module Pup
module Core
module Types

CStrType = LLVM.Pointer(LLVM::Int8)

SymbolType = LLVM::Int

obj_ty = LLVM::Type.opaque  # forward declaration, refined later
ObjectPtrType = LLVM::Type.pointer(obj_ty)

ArgsType = LLVM.Pointer(ObjectPtrType)

MethodType = LLVM.Function([ObjectPtrType, LLVM::Int, ArgsType], ObjectPtrType)

MethodPtrType = LLVM.Pointer(MethodType)
MethodListEntryType = LLVM::Type.rec do |thistype|
  LLVM.Struct(
    SymbolType,  # method name
    MethodPtrType, # method impl
    LLVM.Pointer(thistype)  # next list entry
  )
end
MethodListEntryPtrType = LLVM.Pointer(MethodListEntryType)

AttributeListEntryType = LLVM::Type.rec do |thistype|
  LLVM.Struct(
    SymbolType,  # attribute name
    ObjectPtrType, # attribute value
    LLVM.Pointer(thistype)  # next list entry
  )
end

ClassType = LLVM::Type.rec do |thistype|
  LLVM.Struct(
    obj_ty,  # Since the 'Class' class is a kind of Object
    LLVM.Pointer(thistype),  # superclass
    CStrType,     # class name
    LLVM.Pointer(MethodListEntryType)  # head of the method linked list
  )
end

ObjectType = LLVM.Struct(
  LLVM.Pointer(ClassType),   # the class of this object
  LLVM.Pointer(AttributeListEntryType)  # head of the method linked list
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
