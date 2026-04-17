//===- SHAsmPrinter.cpp - SH LLVM Assembly Printer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SH assembly printer skeleton. Full implementation with pseudo-instruction
// expansion is added in a later patch.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SHInstPrinter.h"
#include "MCTargetDesc/SHMCAsmInfo.h"
#include "MCTargetDesc/SHMCTargetDesc.h"
#include "SH.h"
#include "SHMCInstLower.h"
#include "SHSubtarget.h"
#include "SHTargetMachine.h"
#include "TargetInfo/SHTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

namespace {

class SHAsmPrinter : public AsmPrinter {
public:
  explicit SHAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {}

  StringRef getPassName() const override { return "SH Assembly Printer"; }

  void emitInstruction(const MachineInstr *MI) override;
};

} // end anonymous namespace

void SHAsmPrinter::emitInstruction(const MachineInstr *MI) {
  SHMCInstLower MCInstLowering(OutContext, *this);
  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSHAsmPrinter() {
  RegisterAsmPrinter<SHAsmPrinter> X(getTheSHTarget());
}
