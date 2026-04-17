//===- SHMCInstLower.cpp - Lower MachineInstr to MCInst ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements lowering from MachineInstr to MCInst for SH.
//
//===----------------------------------------------------------------------===//

#include "SHMCInstLower.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

MCSymbol *
SHMCInstLower::GetGlobalAddressSymbol(const MachineOperand &MO) const {
  return AP.getSymbol(MO.getGlobal());
}

MCSymbol *
SHMCInstLower::GetExternalSymbolSymbol(const MachineOperand &MO) const {
  return AP.GetExternalSymbolSymbol(MO.getSymbolName());
}

MCOperand SHMCInstLower::LowerOperand(const MachineOperand &MO) const {
  switch (MO.getType()) {
  default:
    llvm_unreachable("Unknown operand type");

  case MachineOperand::MO_Register:
    if (MO.isImplicit())
      return MCOperand();
    return MCOperand::createReg(MO.getReg());

  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());

  case MachineOperand::MO_MachineBasicBlock:
    return MCOperand::createExpr(
        MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));

  case MachineOperand::MO_GlobalAddress: {
    const MCSymbol *Sym = GetGlobalAddressSymbol(MO);
    const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
    if (MO.getOffset())
      Expr = MCBinaryExpr::createAdd(
          Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
    return MCOperand::createExpr(Expr);
  }

  case MachineOperand::MO_ExternalSymbol: {
    const MCSymbol *Sym = GetExternalSymbolSymbol(MO);
    const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
    if (MO.getOffset())
      Expr = MCBinaryExpr::createAdd(
          Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
    return MCOperand::createExpr(Expr);
  }

  case MachineOperand::MO_BlockAddress: {
    const MCSymbol *Sym = AP.GetBlockAddressSymbol(MO.getBlockAddress());
    const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
    if (MO.getOffset())
      Expr = MCBinaryExpr::createAdd(
          Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
    return MCOperand::createExpr(Expr);
  }

  case MachineOperand::MO_JumpTableIndex:
    return MCOperand::createExpr(
        MCSymbolRefExpr::create(AP.GetJTISymbol(MO.getIndex()), Ctx));

  case MachineOperand::MO_ConstantPoolIndex:
    return MCOperand::createExpr(
        MCSymbolRefExpr::create(AP.GetCPISymbol(MO.getIndex()), Ctx));

  case MachineOperand::MO_RegisterMask:
    return MCOperand();
  }
}

void SHMCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp = LowerOperand(MO);
    if (MCOp.isValid())
      OutMI.addOperand(MCOp);
  }
}
