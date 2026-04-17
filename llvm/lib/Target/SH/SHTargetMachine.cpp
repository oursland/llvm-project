//===- SHTargetMachine.cpp - Define TargetMachine for SH ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH target machine.
//
//===----------------------------------------------------------------------===//

#include "SHTargetMachine.h"
#include "SH.h"
#include "SHISelDAGToDAG.h"
#include "SHMachineFunctionInfo.h"
#include "SHTargetObjectFile.h"
#include "SHTargetTransformInfo.h"
#include "TargetInfo/SHTargetInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/PassRegistry.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSHTarget() {
  RegisterTargetMachine<SHTargetMachine> X(getTheSHTarget());
}

static std::string computeDataLayout(const Triple &TT) {
  // SH SysV ABI: little-endian, 32-bit pointers, max 4-byte alignment.
  // f64:32:32 and i64:32:32 reflect the 4-byte stack alignment —
  // without this, LLVM assumes 8-byte alignment and generates incorrect
  // OR-based address arithmetic for the upper word of 64-bit values.
  // See Renesas SH-4 Software Manual §9 (Calling Conventions).
  return "e-m:e-p:32:32-i8:8:32-i16:16:32-f64:32:32-i64:32:32-n32";
}

static StringRef computeCPU(const Triple &TT, StringRef CPU) {
  if (!CPU.empty())
    return CPU;
  switch (TT.getArch()) {
  case Triple::sh2:
    return "sh2";
  case Triple::sh2a:
    return "sh2a";
  case Triple::sh3:
    return "sh3";
  case Triple::sh3e:
    return "sh3e";
  case Triple::sh4:
    return "sh4";
  case Triple::sh4a:
    return "sh4a";
  case Triple::sh:
  default:
    // Bare `sh-*` triple is the baseline SH1 with no extensions, matching
    // GCC's -m1 default. Users wanting an FPU or a newer ISA level select
    // a CPU explicitly via -mcpu=sh4 / -mcpu=sh2a-fpu / etc.
    return "sh1";
  }
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

SHTargetMachine::SHTargetMachine(const Target &T, const Triple &TT,
                                 StringRef CPU, StringRef FS,
                                 const TargetOptions &Options,
                                 std::optional<Reloc::Model> RM,
                                 std::optional<CodeModel::Model> CM,
                                 CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(
          T, computeDataLayout(TT), TT, computeCPU(TT, CPU), FS, Options,
          getEffectiveRelocModel(RM), CM.value_or(CodeModel::Small), OL),
      TLOF(std::make_unique<SHTargetObjectFile>()),
      Subtarget(TT, std::string(computeCPU(TT, CPU)), std::string(FS), *this) {
  initAsmInfo();

  // Ensure 'unreachable' in LLVM IR emits a trap instruction (TRAPA #0)
  // rather than falling through to stale bytes.
  this->Options.TrapUnreachable = true;
}

SHTargetMachine::~SHTargetMachine() = default;

TargetTransformInfo
SHTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<SHTTIImpl>(this, F));
}

MachineFunctionInfo *SHTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return SHMachineFunctionInfo::create<SHMachineFunctionInfo>(
      Allocator, F, static_cast<const SHSubtarget *>(STI));
}

//===----------------------------------------------------------------------===//
// Pass Pipeline Configuration
//===----------------------------------------------------------------------===//

namespace {
class SHPassConfig : public TargetPassConfig {
public:
  SHPassConfig(SHTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  SHTargetMachine &getSHTargetMachine() const {
    return getTM<SHTargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
  void addPreEmitPass2() override;
};
} // end anonymous namespace

TargetPassConfig *SHTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new SHPassConfig(*this, PM);
}

void SHPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool SHPassConfig::addInstSelector() {
  addPass(createSHISelDag(getSHTargetMachine(), getOptLevel()));
  return false;
}

void SHPassConfig::addPreEmitPass() {
  // Machine passes (DT combine, FPSCR, delay slot filler, branch expansion)
  // are registered in a later patch.
}

void SHPassConfig::addPreEmitPass2() {
  // Constant island pass is registered in a later patch.
}
