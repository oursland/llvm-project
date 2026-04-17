//===- SHMCInstLower.h - Lower MachineInstr to MCInst --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SH_SHMCINSTLOWER_H
#define LLVM_LIB_TARGET_SH_SHMCINSTLOWER_H

#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class AsmPrinter;
class MCContext;
class MCInst;
class MCOperand;
class MCSymbol;
class MachineInstr;

class SHMCInstLower {
  MCContext &Ctx;
  AsmPrinter &AP;

public:
  SHMCInstLower(MCContext &Ctx, AsmPrinter &AP) : Ctx(Ctx), AP(AP) {}

  void Lower(const MachineInstr *MI, MCInst &OutMI) const;

private:
  MCOperand LowerOperand(const MachineOperand &MO) const;
  MCSymbol *GetGlobalAddressSymbol(const MachineOperand &MO) const;
  MCSymbol *GetExternalSymbolSymbol(const MachineOperand &MO) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SH_SHMCINSTLOWER_H
