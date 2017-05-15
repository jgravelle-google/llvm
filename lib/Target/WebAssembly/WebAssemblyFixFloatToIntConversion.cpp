//=== WebAssemblyFixFloatToIntConversion.cpp - ~~~ =//

/* Things to do:

1. Lower fptoui (and fptosi) to wasm intrinsics
  . add wasm_fp_to_ui intrinsic to IntrinsicsWebAssembly.td
  . make a Module-level IR pass (not machine IR)
  . convert fptoui IR instruction into call wasm_fp_to_ui
  . guard the call with a bounds check (>= 0 AND <= UINT_MAX)
  . split the basic blocks for the condition
  . phi from 0 otherwise (or just undef to make it clearer)
  . add pattern to ISel wasm_fp_to_ui into the wasm i32.trunc_u/f64 instrs

2. Emit libcall
  . use compiler-rt to do it in software

3. microbenchmark
  . float conversion is natively slow enough that maybe compiler-rt is faster all the time
  . test it, if so, just do that

  rt function: __fixunsdfdi
  int impl: __fixuint (fairly fast, has relevant checks)
            ^^^^ we should just do this because it's at worst equivalent to adding those checks
  further: see http://stereopsis.com/sree/fpu2006.html (Know your FPU)
*/



#include "WebAssembly.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-fix-float-to-int-conversion"

namespace {
class WebAssemblyFixFloatToIntConversion final : public FunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Fix Float To Int Conversion";
  }

  bool runOnFunction(Function &F) override;

public:
  static char ID;
  WebAssemblyFixFloatToIntConversion() : FunctionPass(ID) {}
};
} // End anonymous namespace

char WebAssemblyFixFloatToIntConversion::ID = 0;
FunctionPass *llvm::createWebAssemblyFixFloatToIntConversion() {
  return new WebAssemblyFixFloatToIntConversion();
}


bool WebAssemblyFixFloatToIntConversion::runOnFunction(Function &F) {
  LLVMContext &C = F.getContext();
  bool DidChange = true;
  while (DidChange) {
    DidChange = false;
    for (auto BBI = F.begin(); BBI != F.end(); ++BBI) {
      auto &BB = *BBI;
      IRBuilder<> IRB(C);
      bool DidChange = false;
      for (auto II = BB.begin(); II != BB.end(); ++II) {
        auto &I = *II;
        unsigned Opc = I.getOpcode();
        switch (Opc) {
        default:
          break;
        case Instruction::FPToUI:
        case Instruction::FPToSI:
          Value *FloatVal = I.getOperand(0);
          auto INext = II;
          ++INext;
          BasicBlock *End = BB.splitBasicBlock(&*INext, "ftoi.end");
          BasicBlock *IfTrue = BB.splitBasicBlock(&I, "ftoi.checked");

          IntegerType *IntType = cast<IntegerType>(I.getType());
          bool IsSigned = Opc == Instruction::FPToSI;
          uint64_t AllBits = IntType->getBitMask();
          uint64_t SignBit = IntType->getSignBit();
          uint64_t IntMin = IsSigned ? AllBits            : 0;
          uint64_t IntMax = IsSigned ? AllBits & ~SignBit : AllBits;
          Constant *LowerBound = ConstantFP::get(FloatVal->getType(), IntMin);
          Constant *UpperBound = ConstantFP::get(FloatVal->getType(), IntMax);

          BB.getTerminator()->eraseFromParent();
          IRB.SetInsertPoint(&BB);
          Value *CmpLo = IRB.CreateFCmpOGE(FloatVal, LowerBound);
          Value *CmpHi = IRB.CreateFCmpOLE(FloatVal, UpperBound);
          Value *And = IRB.CreateAnd(CmpLo, CmpHi);
          IRB.CreateCondBr(And, IfTrue, End);

          IRB.SetInsertPoint(&*INext);
          Constant *DefaultValue = ConstantInt::get(IntType, AllBits);
          PHINode *EndPhi = IRB.CreatePHI(IntType, 2);
          // Set uses before adding incomings to Phi in order to avoid
          // overwriting the Use of I that creates.
          I.replaceAllUsesWith(EndPhi);
          EndPhi->addIncoming(DefaultValue, &BB);
          EndPhi->addIncoming(&I, IfTrue);

          for (auto BI2 = F.begin(); BI2 != F.end(); BI2++) {
            if (&*BI2 == End) {
              --BI2;
              BBI = BI2;
              break;
            }
          }
          DidChange = true;

          break;
        }
        if (DidChange) {
          break;
        }
      }
    }
  }
  return true;
}
