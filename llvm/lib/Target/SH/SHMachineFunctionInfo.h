//===- SHMachineFunctionInfo.h - SH machine function info -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SH_SHMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_SH_SHMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class SHMachineFunctionInfo : public MachineFunctionInfo {
  /// Frame index for the stack slot holding the saved PR (link register).
  int PRSpillFI = 0;
  bool PRSpillFISet = false;

  /// Frame index for the start of varargs area (GPR save).
  int VarArgsFrameIndex = 0;

  /// Frame index for the FPR save area (double varargs).
  int VarArgsFPRSaveAreaFI = 0;

  /// Frame index pointing to the stack overflow area (args beyond regs).
  int VarArgsStackArgsFI = 0;

  /// Number of fixed GPR args consumed before varargs start.
  unsigned NumFixedGPRArgs = 0;

  /// Number of fixed FPR args consumed before varargs start.
  unsigned NumFixedFPRArgs = 0;

  /// Size of the incoming argument area used by vararg functions.
  unsigned VarArgsSize = 0;

public:
  SHMachineFunctionInfo() = default;
  SHMachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  int getPRSpillFI() const { return PRSpillFI; }
  void setPRSpillFI(int FI) {
    PRSpillFI = FI;
    PRSpillFISet = true;
  }
  bool hasPRSpillFI() const { return PRSpillFISet; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }

  int getVarArgsFPRSaveAreaFI() const { return VarArgsFPRSaveAreaFI; }
  void setVarArgsFPRSaveAreaFI(int FI) { VarArgsFPRSaveAreaFI = FI; }

  int getVarArgsStackArgsFI() const { return VarArgsStackArgsFI; }
  void setVarArgsStackArgsFI(int FI) { VarArgsStackArgsFI = FI; }

  unsigned getNumFixedGPRArgs() const { return NumFixedGPRArgs; }
  void setNumFixedGPRArgs(unsigned N) { NumFixedGPRArgs = N; }

  unsigned getNumFixedFPRArgs() const { return NumFixedFPRArgs; }
  void setNumFixedFPRArgs(unsigned N) { NumFixedFPRArgs = N; }

  unsigned getVarArgsSize() const { return VarArgsSize; }
  void setVarArgsSize(unsigned Sz) { VarArgsSize = Sz; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SH_SHMACHINEFUNCTIONINFO_H
