//===- SH.cpp - Implement SH ABI code generation -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements SH ABI-specific code generation, specifically
// the classification of function arguments and return values for the
// SH SysV ABI.
//
// SH ABI summary:
//   - Integer arguments: R4-R7 (4 registers), then stack
//   - Float arguments: FR4-FR11 (in pairs), then stack
//   - Double arguments: DR4, DR6, DR8, DR10, then stack
//   - Return: integers in R0 (i64 in R0:R1), f32 in FR0, f64 in DR0
//   - Aggregates <= 16 bytes: passed by value in GPRs
//   - Aggregates > 16 bytes: passed indirectly via pointer
//   - Aggregate return > 8 bytes: via hidden sret pointer
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// SH ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class SHABIInfo : public DefaultABIInfo {
  // Track how many GPR argument slots remain (R4-R7 = 4 slots).
  struct CCState {
    unsigned FreeRegs;
  };

public:
  SHABIInfo(CodeGen::CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  void computeInfo(CGFunctionInfo &FI) const override {
    CCState State;
    State.FreeRegs = 4;

    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

    unsigned ArgIdx = 0;
    unsigned NumRequired =
        FI.isVariadic() ? FI.getNumRequiredArgs() : FI.arg_size();
    for (auto &I : FI.arguments()) {
      if (ArgIdx >= NumRequired && isAggregateTypeForABI(I.type)) {
        // Variadic aggregate arguments must be passed inline (by value)
        // as {i32, i32, ...} — NOT via byval pointer.
        // GCC SH rule: if the entire struct fits in remaining GPRs,
        // pass in registers. Otherwise, ALL words go on the stack.
        uint64_t Size = getContext().getTypeSize(I.type);
        unsigned NumWords = (Size + 31) / 32;
        llvm::IntegerType *Int32 = llvm::Type::getInt32Ty(getVMContext());
        SmallVector<llvm::Type *, 8> Elements(NumWords, Int32);
        llvm::Type *CoerceTy = llvm::StructType::get(getVMContext(), Elements);
        if (NumWords <= State.FreeRegs) {
          State.FreeRegs -= NumWords;
          I.info = ABIArgInfo::getDirectInReg(CoerceTy);
        } else {
          // Struct doesn't fit in remaining GPRs — ALL words go on the stack.
          // Do NOT zero FreeRegs: remaining GPR slots stay available for
          // subsequent smaller arguments. GCC SH skips the struct to the
          // stack but still uses leftover GPRs for later args (e.g. a long
          // after a struct that didn't fit).
          I.info = ABIArgInfo::getDirect(CoerceTy);
        }
      } else {
        I.info = classifyArgumentType(I.type, State);
      }
      ++ArgIdx;
    }
  }

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType Ty, CCState &State) const;

  // SH passes all variadic arguments by value on the stack, regardless of
  // type.  The default EmitVAArg calls the base-class classifyArgumentType
  // (without CCState) which returns Indirect for aggregates -- causing it
  // to read a *pointer* from the va_list instead of the value itself.
  // Override to always use direct (by-value) access.
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;

private:
  bool shouldUseInReg(QualType Ty, CCState &State) const;
  ABIArgInfo getIndirectResult(QualType Ty, bool ByVal, CCState &State) const;
};

} // end anonymous namespace

bool SHABIInfo::shouldUseInReg(QualType Ty, CCState &State) const {
  // SH ABI: FP args use FR/DR registers independently from GPRs.
  // FP types should NOT consume GPR FreeRegs slots.
  if (Ty->isFloatingType())
    return true; // Always InReg (CC_SH_Custom assigns FR/DR regs)

  unsigned Size = getContext().getTypeSize(Ty);
  unsigned SizeInRegs = llvm::alignTo(Size, 32U) / 32U;

  if (SizeInRegs == 0)
    return false;

  if (SizeInRegs > State.FreeRegs) {
    // Large arg doesn't fit in remaining GPRs — spill to stack.
    // Do NOT zero FreeRegs: remaining slots stay available for
    // subsequent smaller args (matches GCC SH behavior).
    return false;
  }

  State.FreeRegs -= SizeInRegs;
  return true;
}

ABIArgInfo SHABIInfo::getIndirectResult(QualType Ty, bool ByVal,
                                        CCState &State) const {
  if (!ByVal) {
    if (State.FreeRegs) {
      --State.FreeRegs; // Non-byval indirects use one pointer register.
      return getNaturalAlignIndirectInReg(Ty);
    }
    return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                   false);
  }

  // Compute the byval alignment.
  const unsigned MinABIStackAlignInBytes = 4;
  unsigned TypeAlign = getContext().getTypeAlign(Ty) / 8;
  return ABIArgInfo::getIndirect(
      CharUnits::fromQuantity(4),
      /*AddrSpace=*/getDataLayout().getAllocaAddrSpace(), /*ByVal=*/true,
      /*Realign=*/TypeAlign > MinABIStackAlignInBytes);
}

ABIArgInfo SHABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  // Aggregate types > 8 bytes are returned via sret hidden pointer.
  if (isAggregateTypeForABI(RetTy)) {
    // Empty structs are ignored.
    if (isEmptyRecord(getContext(), RetTy, true))
      return ABIArgInfo::getIgnore();

    uint64_t Size = getContext().getTypeSize(RetTy);
    // Small aggregates (<= 8 bytes / 2 registers) can be returned directly
    // by coercing to integer type(s).
    if (Size <= 32) {
      return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));
    } else if (Size <= 64) {
      llvm::Type *Int32Ty = llvm::Type::getInt32Ty(getVMContext());
      return ABIArgInfo::getDirect(
          llvm::StructType::get(getVMContext(), {Int32Ty, Int32Ty}));
    }
    // Larger aggregates: return indirectly via sret.
    return getNaturalAlignIndirect(RetTy, getDataLayout().getAllocaAddrSpace());
  }

  // Treat enums as their underlying type.
  if (const auto *ED = RetTy->getAsEnumDecl())
    RetTy = ED->getIntegerType();

  // Promote small integer types.
  if (isPromotableIntegerTypeForABI(RetTy))
    return ABIArgInfo::getExtend(RetTy);

  return ABIArgInfo::getDirect();
}

ABIArgInfo SHABIInfo::classifyArgumentType(QualType Ty, CCState &State) const {
  // Check C++ ABI requirements first.
  const RecordType *RT = Ty->getAsCanonical<RecordType>();
  if (RT) {
    CGCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI());
    if (RAA == CGCXXABI::RAA_Indirect) {
      return getIndirectResult(Ty, /*ByVal=*/false, State);
    } else if (RAA == CGCXXABI::RAA_DirectInMemory) {
      return getNaturalAlignIndirect(
          Ty, /*AddrSpace=*/getDataLayout().getAllocaAddrSpace(),
          /*ByVal=*/true);
    }
  }

  if (isAggregateTypeForABI(Ty)) {
    // Structures with flexible arrays are always indirect.
    if (RT && RT->getDecl()->getDefinitionOrSelf()->hasFlexibleArrayMember())
      return getIndirectResult(Ty, /*ByVal=*/true, State);

    // Ignore empty structs/unions.
    if (isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();

    unsigned SizeInRegs = (getContext().getTypeSize(Ty) + 31) / 32;

    // Try to pass small aggregates in registers.
    if (SizeInRegs <= State.FreeRegs) {
      llvm::IntegerType *Int32 = llvm::Type::getInt32Ty(getVMContext());
      SmallVector<llvm::Type *, 4> Elements(SizeInRegs, Int32);
      llvm::Type *Result = llvm::StructType::get(getVMContext(), Elements);
      State.FreeRegs -= SizeInRegs;
      return ABIArgInfo::getDirectInReg(Result);
    }

    // Struct doesn't fit in remaining GPRs.
    // Special case: va_list (__va_list_tag) must be passed by value on the
    // stack as individual words, matching GCC SH's ABI.  The SH backend
    // has no byval support, so coerce to {i32,...} for va_list.
    // All other aggregates use the normal indirect (byval pointer) path.
    bool IsVaList = false;
    if (RT) {
      if (auto *II = RT->getDecl()->getIdentifier())
        IsVaList = II->getName() == "__va_list_tag";
    }
    if (IsVaList) {
      // Pass the 20-byte va_list byval on the stack, matching GCC SH ABI.
      // We zero FreeRegs so that subsequent arguments (if any) go to the stack.
      State.FreeRegs = 0;
      return getIndirectResult(Ty, /*ByVal=*/true, State);
    }
    // Non-va_list aggregates: pass by value on the stack (byval).
    // GCC SH ABI: large structs go directly on the stack WITHOUT
    // consuming a GPR for a pointer. Don't decrement FreeRegs.
    return getIndirectResult(Ty, /*ByVal=*/true, State);
  }

  // Treat enums as their underlying type.
  if (const auto *ED = Ty->getAsEnumDecl())
    Ty = ED->getIntegerType();

  bool InReg = shouldUseInReg(Ty, State);

  // Don't pass > 64 bit integers in registers.
  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() > 64)
      return getIndirectResult(Ty, /*ByVal=*/true, State);

  if (isPromotableIntegerTypeForABI(Ty)) {
    if (InReg)
      return ABIArgInfo::getDirectInReg();
    return ABIArgInfo::getExtend(Ty);
  }
  if (InReg)
    return ABIArgInfo::getDirectInReg();
  return ABIArgInfo::getDirect();
}

RValue SHABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                            QualType Ty, AggValueSlot Slot) const {
  // SH va_list is a struct __va_list_tag { void *next_o, *next_o_limit,
  //   *next_fp, *next_fp_limit, *next_stack; };
  // typedef __va_list_tag va_list[1];  -- so VAListAddr points to the struct.
  //
  // For FP types: read from next_fp, fall back to next_stack.
  // For all other types (including aggregates): read from next_o,
  // fall back to next_stack. Aggregates are passed inline as {i32,...}
  // by the caller, so the full struct value is in the save area.
  CGBuilderTy &Builder = CGF.Builder;

  // Determine if this is an FP type (read from FP save area) or GPR type.
  bool IsFP = Ty->isFloatingType();

  // Field indices in the va_list struct:
  //   0: __va_next_o       2: __va_next_fp      4: __va_next_stack
  //   1: __va_next_o_limit 3: __va_next_fp_limit
  unsigned NextIdx = IsFP ? 2 : 0;
  unsigned LimitIdx = IsFP ? 3 : 1;

  // Compute the argument size (rounded up to 4-byte alignment).
  uint64_t TySize = CGF.getContext().getTypeSize(Ty) / 8;
  uint64_t ArgSize = llvm::alignTo(TySize, 4);

  // Load the current pointer and limit.
  Address NextPtrP = Builder.CreateStructGEP(VAListAddr, NextIdx, "va.next_p");
  llvm::Value *NextPtr = Builder.CreateLoad(NextPtrP, "va.next");

  Address LimitP = Builder.CreateStructGEP(VAListAddr, LimitIdx, "va.limit_p");
  llvm::Value *Limit = Builder.CreateLoad(LimitP, "va.limit");

  // For doubles (8 bytes), align the current pointer to 4-byte boundary
  // (SH doubles are 4-byte aligned, which should already be the case,
  // but handle odd-offset cases for f64 within the FP area).
  if (IsFP && TySize == 8) {
    // Align to 4 bytes (already guaranteed by our layout, but be safe).
    llvm::Value *NextPtrInt = Builder.CreatePtrToInt(NextPtr, CGF.Int32Ty);
    NextPtrInt = Builder.CreateAdd(NextPtrInt, Builder.getInt32(3));
    NextPtrInt = Builder.CreateAnd(NextPtrInt, Builder.getInt32(~3));
    NextPtr = Builder.CreateIntToPtr(NextPtrInt, NextPtr->getType());
  }

  // Compute the new pointer after reading this arg.
  llvm::Value *NewNextPtr = Builder.CreateGEP(
      CGF.Int8Ty, NextPtr, llvm::ConstantInt::get(CGF.Int32Ty, ArgSize),
      "va.next.new");

  // Check if the argument fits in the register save area.
  // If NewNextPtr > Limit, we must use the stack overflow area.
  llvm::Value *UsingStack =
      Builder.CreateICmpUGT(NewNextPtr, Limit, "va.use_stack");

  llvm::BasicBlock *InRegBlock = CGF.createBasicBlock("va.in_reg");
  llvm::BasicBlock *OnStackBlock = CGF.createBasicBlock("va.on_stack");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("va.end");

  Builder.CreateCondBr(UsingStack, OnStackBlock, InRegBlock);

  // --- In register save area ---
  CGF.EmitBlock(InRegBlock);
  // Update the next pointer.
  Builder.CreateStore(NewNextPtr, NextPtrP);
  CGF.EmitBranch(ContBlock);

  // --- On stack (overflow area) ---
  CGF.EmitBlock(OnStackBlock);
  Address StackPtrP = Builder.CreateStructGEP(VAListAddr, 4, "va.stack_p");
  llvm::Value *StackPtr = Builder.CreateLoad(StackPtrP, "va.stack");

  // Align stack pointer for the argument type.
  if (TySize >= 4) {
    llvm::Value *StackPtrInt = Builder.CreatePtrToInt(StackPtr, CGF.Int32Ty);
    StackPtrInt = Builder.CreateAdd(StackPtrInt, Builder.getInt32(3));
    StackPtrInt = Builder.CreateAnd(StackPtrInt, Builder.getInt32(~3));
    StackPtr = Builder.CreateIntToPtr(StackPtrInt, StackPtr->getType());
  }

  // Advance the stack pointer.
  llvm::Value *NewStackPtr = Builder.CreateGEP(
      CGF.Int8Ty, StackPtr, llvm::ConstantInt::get(CGF.Int32Ty, ArgSize),
      "va.stack.new");
  Builder.CreateStore(NewStackPtr, StackPtrP);
  CGF.EmitBranch(ContBlock);

  // --- Merge ---
  CGF.EmitBlock(ContBlock);
  llvm::Type *MemTy = CGF.ConvertTypeForMem(Ty);
  llvm::PHINode *ArgAddr = Builder.CreatePHI(
      llvm::PointerType::getUnqual(MemTy->getContext()), 2, "va.addr");
  ArgAddr->addIncoming(NextPtr, InRegBlock);
  ArgAddr->addIncoming(StackPtr, OnStackBlock);

  CharUnits ArgAlign = CharUnits::fromQuantity(std::max<uint64_t>(TySize, 4));
  if (ArgAlign > CharUnits::fromQuantity(4))
    ArgAlign = CharUnits::fromQuantity(4); // SH max stack alignment is 4
  Address ResultAddr(ArgAddr, MemTy, ArgAlign);

  return CGF.EmitLoadOfAnyValue(CGF.MakeAddrLValue(ResultAddr, Ty), Slot);
}

//===----------------------------------------------------------------------===//
// SH TargetCodeGenInfo
//===----------------------------------------------------------------------===//

namespace {
class SHTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  SHTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<SHABIInfo>(CGT)) {}
};
} // end anonymous namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createSHTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<SHTargetCodeGenInfo>(CGM.getTypes());
}
