//===- SHSubtarget.cpp - SH Subtarget Information -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH subtarget interface.
//
//===----------------------------------------------------------------------===//

#include "SHSubtarget.h"
#include "SH.h"
#include "SHSelectionDAGInfo.h"

using namespace llvm;

#define DEBUG_TYPE "sh-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "SHGenSubtargetInfo.inc"

void SHSubtarget::anchor() {}

SHSubtarget &SHSubtarget::initializeSubtargetFeatures(StringRef CPU,
                                                      StringRef FS) {
  ParseSubtargetFeatures(CPU, /*TuneCPU=*/CPU, FS);
  return *this;
}

SHSubtarget::SHSubtarget(const Triple &TT, const std::string &CPU,
                         const std::string &FS, const TargetMachine &TM)
    : SHGenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS), InstrInfo(*this),
      FrameLowering(*this), TLInfo(TM, initializeSubtargetFeatures(CPU, FS)) {
  TSInfo = std::make_unique<SHSelectionDAGInfo>();
}

SHSubtarget::~SHSubtarget() = default;

const SelectionDAGTargetInfo *SHSubtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}
