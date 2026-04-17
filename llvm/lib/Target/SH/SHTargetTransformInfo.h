//===- SHTargetTransformInfo.h - SH TTI ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SH_SHTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_SH_SHTARGETTRANSFORMINFO_H

#include "SHTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"

namespace llvm {

class SHTTIImpl final : public BasicTTIImplBase<SHTTIImpl> {
  using BaseT = BasicTTIImplBase<SHTTIImpl>;
  friend BaseT;

  const SHSubtarget *ST;
  const SHTargetLowering *TLI;

  const SHSubtarget *getST() const { return ST; }
  const SHTargetLowering *getTLI() const { return TLI; }

public:
  explicit SHTTIImpl(const SHTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  // Provide value semantics. MSVC requires that we spell all of these out.
  SHTTIImpl(const SHTTIImpl &Arg)
      : BaseT(static_cast<const BaseT &>(Arg)), ST(Arg.ST), TLI(Arg.TLI) {}
  SHTTIImpl(SHTTIImpl &&Arg)
      : BaseT(std::move(static_cast<BaseT &>(Arg))), ST(std::move(Arg.ST)),
        TLI(std::move(Arg.TLI)) {}

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TargetTransformInfo::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE) const override {
    BaseT::getUnrollingPreferences(L, SE, UP, ORE);
    // SH has limited PC-relative range (1020 bytes for mov.l @(disp,PC)).
    // The constant island pass can split long functions, but unrolling
    // dramatically inflates code size, making many constant pool entries
    // unreachable and causing SEGVs.  Keep unrolling very conservative.
    UP.Threshold = 8;
    UP.PartialThreshold = 0;
    UP.OptSizeThreshold = 0;
    UP.PartialOptSizeThreshold = 0;
    UP.Runtime = false;
    UP.UpperBound = false;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SH_SHTARGETTRANSFORMINFO_H
