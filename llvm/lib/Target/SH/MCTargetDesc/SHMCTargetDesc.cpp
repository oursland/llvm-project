//===- SHMCTargetDesc.cpp - SH Target Descriptions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides SH-specific MC target descriptions.
//
//===----------------------------------------------------------------------===//

#include "SHMCTargetDesc.h"
#include "SHInstPrinter.h"
#include "SHMCAsmInfo.h"
#include "TargetInfo/SHTargetInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "SHGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "SHGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "SHGenRegisterInfo.inc"

static MCInstrInfo *createSHMCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitSHMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createSHMCRegisterInfo(const Triple &TT) {
  auto *X = new MCRegisterInfo();
  // PR is the return address register (DWARF reg 17)
  InitSHMCRegisterInfo(X, SH::PR);
  return X;
}

static MCSubtargetInfo *createSHMCSubtargetInfo(const Triple &TT, StringRef CPU,
                                                StringRef FS) {
  return createSHMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCAsmInfo *createSHMCAsmInfo(const MCRegisterInfo &MRI, const Triple &TT,
                                    const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new SHMCAsmInfo(TT);
  // Initial state of the frame pointer is SP (R15).
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(
      nullptr, MRI.getDwarfRegNum(SH::R15, true), 0);
  MAI->addInitialFrameState(Inst);
  return MAI;
}

static MCInstPrinter *createSHMCInstPrinter(const Triple &T,
                                            unsigned SyntaxVariant,
                                            const MCAsmInfo &MAI,
                                            const MCInstrInfo &MII,
                                            const MCRegisterInfo &MRI) {
  return new SHInstPrinter(MAI, MII, MRI);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSHTargetMC() {
  Target &TheSHTarget = getTheSHTarget();

  TargetRegistry::RegisterMCAsmInfo(TheSHTarget, createSHMCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(TheSHTarget, createSHMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(TheSHTarget, createSHMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(TheSHTarget, createSHMCSubtargetInfo);
  TargetRegistry::RegisterMCCodeEmitter(TheSHTarget, createSHMCCodeEmitter);
  TargetRegistry::RegisterMCInstPrinter(TheSHTarget, createSHMCInstPrinter);
  TargetRegistry::RegisterMCAsmBackend(TheSHTarget, createSHAsmBackend);
}
