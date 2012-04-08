#include "llvm/CodeGen/GCStrategy.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

namespace {
  class LLVM_LIBRARY_VISIBILITY PupGC : public GCStrategy {
  public:
    PupGC() {
      InitRoots = true;
      UsesMetadata = true;
      NeededSafePoints = 1 << GC::Loop
		       | 1 << GC::Return
		       | 1 << GC::PreCall
		       | 1 << GC::PostCall;
      //CustomRoots = true;
      //CustomReadBarriers = true;
      //CustomWriteBarriers = true;
    }
  };
  
  GCRegistry::Add<PupGC>
  X("pupgc", "pup garbage collector.");
}
