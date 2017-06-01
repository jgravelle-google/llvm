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
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-fix-float-to-int-conversion"

namespace {
class WebAssemblyFixFloatToIntConversion final : public BasicBlockPass {
  StringRef getPassName() const override {
    return "WebAssembly Fix Float To Int Conversion";
  }

  bool runOnBasicBlock(BasicBlock &BB) override;

public:
  static char ID;
  WebAssemblyFixFloatToIntConversion() : BasicBlockPass(ID) {}
};
} // End anonymous namespace

char WebAssemblyFixFloatToIntConversion::ID = 0;
BasicBlockPass *llvm::createWebAssemblyFixFloatToIntConversion() {
  return new WebAssemblyFixFloatToIntConversion();
}

/// Helper struct to hold computed values from FtoI Instruction
struct InstrData {
  Instruction &Instr;
  bool IsSigned;
  Value *Float;
  Type *FloatType;
  IntegerType *IntType;

  InstrData(Instruction &I) : Instr(I) {
    IsSigned = (I.getOpcode() == Instruction::FPToSI);
    Float = I.getOperand(0);
    FloatType = Float->getType();
    IntType = cast<IntegerType>(I.getType());
  }
};

static void lowerFtoi(BasicBlock &BB, InstrData const &IData);
static BasicBlock* createBoundsCheck(IRBuilder<> &IRB, BasicBlock &BB,
                                     InstrData const &IData,
                                     Instruction *INext);
static Function* getIntrinsicFunction(InstrData const &IData);

bool WebAssemblyFixFloatToIntConversion::runOnBasicBlock(BasicBlock &BB) {
  for (Instruction &I : BB) {
    switch (I.getOpcode()) {
    default:
      break;
    case Instruction::FPToUI:
    case Instruction::FPToSI:
      InstrData IData(I);
      if (IData.FloatType->isFP128Ty()) {
        // Long double ops are converted to libcalls, so don't touch them here.
        continue;
      }

      lowerFtoi(BB, IData);
      return true;
    }
  }
  return false;
}

static void lowerFtoi(BasicBlock &BB, InstrData const &IData) {
  IRBuilder<> IRB(BB.getContext());
  auto INext = IData.Instr.getIterator();
  ++INext;

  BasicBlock *IfTrue = createBoundsCheck(IRB, BB, IData, &*INext);

  IRB.SetInsertPoint(&IData.Instr);
  SmallVector<Value*, 4> CallArgs;
  CallArgs.push_back(IData.Float);
  Function *IntrinsicFunc = getIntrinsicFunction(IData);
  Instruction *Call = IRB.CreateCall(IntrinsicFunc, CallArgs);

  IRB.SetInsertPoint(&*INext);
  uint64_t AllBits = IData.IntType->getBitMask();
  Constant *DefaultValue = ConstantInt::get(IData.IntType, AllBits);
  PHINode *EndPhi = IRB.CreatePHI(IData.IntType, 2);
  EndPhi->addIncoming(DefaultValue, &BB);
  EndPhi->addIncoming(Call, IfTrue);

  IData.Instr.replaceAllUsesWith(EndPhi);
  IData.Instr.eraseFromParent();
}

static BasicBlock* createBoundsCheck(IRBuilder<> &IRB, BasicBlock &BB,
                                     InstrData const &IData,
                                     Instruction *INext) {
  BasicBlock *End = BB.splitBasicBlock(INext, "ftoi.end");
  BasicBlock *IfTrue = BB.splitBasicBlock(&IData.Instr, "ftoi.checked");

  uint64_t AllBits = IData.IntType->getBitMask();
  uint64_t SignBit = IData.IntType->getSignBit();
  uint64_t IntMin = IData.IsSigned ? AllBits            : 0;
  uint64_t IntMax = IData.IsSigned ? AllBits & ~SignBit : AllBits;
  Constant *LowerBound = ConstantFP::get(IData.FloatType, IntMin);
  Constant *UpperBound = ConstantFP::get(IData.FloatType, IntMax);

  IRB.SetInsertPoint(&BB);
  BB.getTerminator()->eraseFromParent();
  Value *CmpLo = IRB.CreateFCmpOGE(IData.Float, LowerBound);
  Value *CmpHi = IRB.CreateFCmpOLE(IData.Float, UpperBound);
  Value *And = IRB.CreateAnd(CmpLo, CmpHi);
  IRB.CreateCondBr(And, IfTrue, End);

  return IfTrue;
}

static Function* getIntrinsicFunction(InstrData const &IData) {
  SmallVector<Type*, 4> IntrinsicTypes;
  IntrinsicTypes.push_back(IData.IntType);
  IntrinsicTypes.push_back(IData.FloatType);
  Intrinsic::ID Intrin = IData.IsSigned ? Intrinsic::wasm_trapping_ftosi
                                       : Intrinsic::wasm_trapping_ftoui;
  Module *M = IData.Instr.getModule();
  return Intrinsic::getDeclaration(M, Intrin, IntrinsicTypes);
}
