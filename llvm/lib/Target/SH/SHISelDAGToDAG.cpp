//===- SHISelDAGToDAG.cpp - SH DAG Instruction Selector ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SH DAG-to-DAG instruction selector skeleton. Most selection is handled
// by tablegen-generated patterns. Custom selection added in a later patch.
//
//===----------------------------------------------------------------------===//

#include "SHISelDAGToDAG.h"
#include "SH.h"
#include "SHSubtarget.h"
#include "SHTargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "sh-isel"
#define PASS_NAME "SH DAG->DAG Pattern Instruction Selection"

namespace {

class SHDAGToDAGISel : public SelectionDAGISel {
public:
  SHDAGToDAGISel() = delete;

  explicit SHDAGToDAGISel(SHTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  void Select(SDNode *N) override;

  bool SelectAddr(SDValue Addr, SDValue &Base, SDValue &Offset);

  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override;

#define GET_DAGISEL_DECL
#include "SHGenDAGISel.inc"
};

#define GET_DAGISEL_BODY SHDAGToDAGISel
#include "SHGenDAGISel.inc"

class SHDAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  explicit SHDAGToDAGISelLegacy(SHTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(ID,
                               std::make_unique<SHDAGToDAGISel>(TM, OptLevel)) {
  }
};

char SHDAGToDAGISelLegacy::ID;

} // end anonymous namespace

INITIALIZE_PASS(SHDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

bool SHDAGToDAGISel::SelectAddr(SDValue Addr, SDValue &Base, SDValue &Offset) {
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, SDLoc(Addr), MVT::i32);
  return true;
}

bool SHDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  OutOps.push_back(Op);
  return false;
}

void SHDAGToDAGISel::Select(SDNode *N) { SelectCode(N); }

FunctionPass *llvm::createSHISelDag(SHTargetMachine &TM,
                                    CodeGenOptLevel OptLevel) {
  return new SHDAGToDAGISelLegacy(TM, OptLevel);
}
