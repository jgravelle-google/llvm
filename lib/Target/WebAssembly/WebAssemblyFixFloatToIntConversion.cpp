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

using InstrIter = SymbolTableList<Instruction>::iterator;

void lowerFtoi(BasicBlock &BB, InstrIter II);
Function* getIntrinsicFunction(Function *F, Type *IntType, Type *FloatType, bool IsSigned);

bool WebAssemblyFixFloatToIntConversion::runOnBasicBlock(BasicBlock &BB) {
  for (InstrIter II = BB.begin(); II != BB.end(); ++II) {
    Instruction &I = *II;
    switch (I.getOpcode()) {
    default:
      break;
    case Instruction::FPToUI:
    case Instruction::FPToSI:
      Value *FloatVal = I.getOperand(0);
      Type *FloatType = FloatVal->getType();
      if (FloatType->isFP128Ty()) {
        continue;
      }

      lowerFtoi(BB, II);
      return true;
    }
  }
  return false;
}

void lowerFtoi(BasicBlock &BB, InstrIter II) {
  Instruction &I = *II;
  bool IsSigned = (I.getOpcode() == Instruction::FPToSI);
  Value *Float = I.getOperand(0);
  Type *FloatType = Float->getType();
  IntegerType *IntType = cast<IntegerType>(I.getType());

  Function *F = BB.getParent();
  LLVMContext &C = F->getContext();
  IRBuilder<> IRB(C);
  InstrIter INext = II;
  ++INext;

  BasicBlock *End = BB.splitBasicBlock(&*INext, "ftoi.end");
  BasicBlock *IfTrue = BB.splitBasicBlock(&I, "ftoi.checked");

  uint64_t AllBits = IntType->getBitMask();
  uint64_t SignBit = IntType->getSignBit();
  uint64_t IntMin = IsSigned ? AllBits            : 0;
  uint64_t IntMax = IsSigned ? AllBits & ~SignBit : AllBits;
  Constant *LowerBound = ConstantFP::get(FloatType, IntMin);
  Constant *UpperBound = ConstantFP::get(FloatType, IntMax);

  BB.getTerminator()->eraseFromParent();
  IRB.SetInsertPoint(&BB);
  Value *CmpLo = IRB.CreateFCmpOGE(Float, LowerBound);
  Value *CmpHi = IRB.CreateFCmpOLE(Float, UpperBound);
  Value *And = IRB.CreateAnd(CmpLo, CmpHi);
  IRB.CreateCondBr(And, IfTrue, End);

  IRB.SetInsertPoint(&I);
  SmallVector<Value*, 4> CallArgs;
  CallArgs.push_back(Float);
  Function *IntrinsicFunc = getIntrinsicFunction(F, IntType, FloatType, IsSigned);
  Instruction *Call = IRB.CreateCall(IntrinsicFunc, CallArgs);

  IRB.SetInsertPoint(&*INext);
  Constant *DefaultValue = ConstantInt::get(IntType, AllBits);
  PHINode *EndPhi = IRB.CreatePHI(IntType, 2);
  EndPhi->addIncoming(DefaultValue, &BB);
  EndPhi->addIncoming(Call, IfTrue);

  I.replaceAllUsesWith(EndPhi);
  I.eraseFromParent();
}

Function* getIntrinsicFunction(Function *F, Type *IntType, Type *FloatType, bool IsSigned) {
  SmallVector<Type*, 4> IntrinsicTypes;
  IntrinsicTypes.push_back(IntType);
  IntrinsicTypes.push_back(FloatType);
  Intrinsic::ID Intrin = IsSigned ? Intrinsic::wasm_trapping_ftosi
                                  : Intrinsic::wasm_trapping_ftoui;
  Module *M = F->getParent();
  return Intrinsic::getDeclaration(M, Intrin, IntrinsicTypes);
}
