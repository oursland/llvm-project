//===- SHInstPrinter.cpp - SH MCInst to assembly syntax -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH MC instruction printer.
//
//===----------------------------------------------------------------------===//

#include "SHInstPrinter.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "SHGenAsmWriter.inc"

void SHInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  OS << getRegisterName(Reg);
}

void SHInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                              StringRef Annot, const MCSubtargetInfo &STI,
                              raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void SHInstPrinter::printOperand(const MCInst *MI, unsigned OpNum,
                                 raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNum);
  if (Op.isReg()) {
    printRegName(O, Op.getReg());
    return;
  }
  if (Op.isImm()) {
    O << '#' << Op.getImm();
    return;
  }
  assert(Op.isExpr() && "unknown operand kind");
  MAI.printExpr(O, *Op.getExpr());
}

void SHInstPrinter::printMemOperand(const MCInst *MI, unsigned OpNum,
                                    raw_ostream &O) {
  // @Rn form
  O << "@";
  printRegName(O, MI->getOperand(OpNum).getReg());
}

void SHInstPrinter::printDispOperand(const MCInst *MI, unsigned OpNum,
                                     raw_ostream &O) {
  // @(disp, Rn) form
  const MCOperand &Disp = MI->getOperand(OpNum);
  const MCOperand &Base = MI->getOperand(OpNum + 1);
  O << "@(";
  if (Disp.isImm())
    O << Disp.getImm();
  else
    MAI.printExpr(O, *Disp.getExpr());
  O << ",";
  printRegName(O, Base.getReg());
  O << ")";
}

void SHInstPrinter::printBrTarget(const MCInst *MI, unsigned OpNum,
                                  raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNum);

  if (Op.isImm()) {
    // Print as hex address (absolute target from the disassembler)
    O << format_hex(static_cast<uint64_t>(Op.getImm()), 0);
  } else {
    // If it's a symbol (like a function name or label), print the expression
    assert(Op.isExpr() && "Unknown operand kind in printBrTarget");
    MAI.printExpr(O, *Op.getExpr());
  }
}
