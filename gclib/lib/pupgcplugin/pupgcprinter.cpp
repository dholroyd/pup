#include "llvm/CodeGen/GCMetadataPrinter.h"
#include "llvm/Support/Compiler.h"

#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/Function.h"
#include "llvm/Target/Mangler.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/ADT/SmallString.h"

using namespace llvm;

namespace {
  class LLVM_LIBRARY_VISIBILITY MyGCPrinter : public GCMetadataPrinter {
  public:
    void beginAssembly(AsmPrinter &AP) {
      // Nothing to do.
    }

    void finishAssembly(AsmPrinter &AP) {
      unsigned IntPtrSize = AP.TM.getTargetData()->getPointerSize();

      int AddressAlignLog = IntPtrSize == 4 ? 2 : 3;
      
      // Put this in the data section.
      AP.OutStreamer.SwitchSection(AP.getObjFileLowering().getDataSection());
      
      // For each function...
      for (iterator FI = begin(), FE = end(); FI != FE; ++FI) {
        GCFunctionInfo &MD = **FI;
        
        // Emit this data structure:
        // 
        // struct {
        //   int32_t PointCount;
        //   struct {
        //     void *SafePointAddress;
        //     int32_t LiveCount;
        //     int32_t LiveOffsets[LiveCount];
        //   } Points[PointCount];
        // } __gcmap_<FUNCTIONNAME>;
        
        // Align to address width.
        AP.EmitAlignment(AddressAlignLog);
        
        // Emit the symbol by which the stack map entry can be found.
        //std::string Symbol;
        //Symbol += MCAI.getGlobalPrefix();
        //Symbol += "__gcmap_";
        //Symbol += MD.getFunction().getName();
        //if (const char *GlobalDirective = MCAI.getGlobalDirective())
        //  OS << GlobalDirective << Symbol << "\n";
        //OS << MCAI.getGlobalPrefix() << Symbol << ":\n";

	std::string SymName;
	SymName += "__gcmap_";
	SymName += MD.getFunction().getName();
	SmallString<128> TmpStr;
	AP.Mang->getNameWithPrefix(TmpStr, SymName);
	MCSymbol *Sym = AP.OutContext.GetOrCreateSymbol(TmpStr);
	AP.OutStreamer.EmitSymbolAttribute(Sym, MCSA_Global);
        AP.OutStreamer.AddComment("live roots for " +
                                  Twine(MD.getFunction().getName()));
	AP.OutStreamer.EmitLabel(Sym);

        //AP.OutStreamer.AddBlankLine();


        
        // Emit PointCount.
        AP.OutStreamer.AddComment("safe point count");
        AP.EmitInt32(MD.size());
        
        // And each safe point...
        for (GCFunctionInfo::iterator PI = MD.begin(),
                                         PE = MD.end(); PI != PE; ++PI) {
          // Align to address width.
          AP.EmitAlignment(AddressAlignLog);
          
          // Emit the address of the safe point.
          AP.OutStreamer.AddComment("safe point address");
          AP.OutStreamer.EmitSymbolValue(PI->Label, IntPtrSize, 0);
          
          // Emit the stack frame size.
          AP.OutStreamer.AddComment("stack frame size");
          AP.EmitInt32(MD.getFrameSize());
          
          // Emit the number of live roots in the function.
          AP.OutStreamer.AddComment("live root count");
          AP.EmitInt32(MD.live_size(PI));
          
          // And for each live root...
          for (GCFunctionInfo::live_iterator LI = MD.live_begin(PI),
                                                LE = MD.live_end(PI);
                                                LI != LE; ++LI) {
            // Print its offset within the stack frame.
            AP.OutStreamer.AddComment("stack offset");
            AP.EmitInt32(LI->StackOffset);
          }
        }
      }
    }
//    virtual void beginAssembly(std::ostream &OS, AsmPrinter &AP,
//                               const MCAsmInfo &MCAI);
//  
//    virtual void finishAssembly(std::ostream &OS, AsmPrinter &AP,
//                                const MCAsmInfo &MCAI);
  };
  
  GCMetadataPrinterRegistry::Add<MyGCPrinter>
  X("pupgc", "Pup garbage collector.");
}

