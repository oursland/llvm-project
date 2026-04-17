//===- SHSubtarget.h - Define Subtarget for the SH --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SH_SHSUBTARGET_H
#define LLVM_LIB_TARGET_SH_SHSUBTARGET_H

#include "SHFrameLowering.h"
#include "SHISelLowering.h"
#include "SHInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "SHGenSubtargetInfo.inc"

namespace llvm {

class StringRef;
class TargetMachine;

class SHSubtarget : public SHGenSubtargetInfo {
  virtual void anchor();

  bool IsSH2 = false;
  bool IsSH2A = false;
  bool IsSH3 = false;
  bool IsSH4 = false;
  bool IsSH4A = false;
  bool HasFPU = false;
  bool HasDPFPU = false;

  SHInstrInfo InstrInfo;
  SHFrameLowering FrameLowering;

  // initializeSubtargetFeatures - Helper to parse features before TLInfo
  // construction. Must be called before TLInfo is initialized because
  // SHTargetLowering reads feature flags (hasFPU, hasDPFPU) in its
  // constructor to decide which register classes to add.
  SHSubtarget &initializeSubtargetFeatures(StringRef CPU, StringRef FS);

  SHTargetLowering TLInfo;
  std::unique_ptr<const SelectionDAGTargetInfo> TSInfo;

public:
  SHSubtarget(const Triple &TT, const std::string &CPU, const std::string &FS,
              const TargetMachine &TM);
  ~SHSubtarget() override;

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  const SHInstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const SHFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const SHTargetLowering *getTargetLowering() const override { return &TLInfo; }
  const SHRegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override;

  bool isSH2() const { return IsSH2; }
  bool isSH2A() const { return IsSH2A; }
  bool isSH3() const { return IsSH3; }
  bool isSH4() const { return IsSH4; }
  bool isSH4A() const { return IsSH4A; }
  bool hasFPU() const { return HasFPU; }
  bool hasDPFPU() const { return HasDPFPU; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SH_SHSUBTARGET_H
