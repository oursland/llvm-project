//===-- SHConstantPoolValue.cpp - SH constant pool value ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements SH-specific constant pool value types.
//
//===----------------------------------------------------------------------===//

#include "SHConstantPoolValue.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// SHConstantPoolValue
//===----------------------------------------------------------------------===//

SHConstantPoolValue::SHConstantPoolValue(Type *Ty, SHCP::SHCPKind Kind)
    : MachineConstantPoolValue(Ty), Kind(Kind) {}

SHConstantPoolValue::SHConstantPoolValue(LLVMContext &C, SHCP::SHCPKind Kind)
    : MachineConstantPoolValue(Type::getInt32Ty(C)), Kind(Kind) {}

SHConstantPoolValue::~SHConstantPoolValue() = default;

int SHConstantPoolValue::getExistingMachineCPValue(MachineConstantPool *CP,
                                                   Align Alignment) {
  return -1;
}

void SHConstantPoolValue::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  ID.AddInteger(Kind);
}

void SHConstantPoolValue::print(raw_ostream &O) const { O << "SHCPV"; }

//===----------------------------------------------------------------------===//
// SHConstantPoolGV
//===----------------------------------------------------------------------===//

SHConstantPoolGV::SHConstantPoolGV(const GlobalValue *GV,
                                   SHCP::SHCPModifier Mod)
    : SHConstantPoolValue(GV->getType(), SHCP::CPGlobalValue), GV(GV),
      Modifier(Mod) {}

SHConstantPoolGV *SHConstantPoolGV::Create(const GlobalValue *GV,
                                           SHCP::SHCPModifier Mod) {
  return new SHConstantPoolGV(GV, Mod);
}

int SHConstantPoolGV::getExistingMachineCPValue(MachineConstantPool *CP,
                                                Align Alignment) {
  const std::vector<MachineConstantPoolEntry> &Constants = CP->getConstants();
  for (unsigned i = 0, e = Constants.size(); i != e; ++i) {
    if (Constants[i].isMachineConstantPoolEntry() &&
        Constants[i].getAlign() >= Alignment) {
      auto *V =
          static_cast<SHConstantPoolValue *>(Constants[i].Val.MachineCPVal);
      if (auto *GVV = dyn_cast<SHConstantPoolGV>(V))
        if (GVV->GV == GV && GVV->Modifier == Modifier)
          return i;
    }
  }
  return -1;
}

void SHConstantPoolGV::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  SHConstantPoolValue::addSelectionDAGCSEId(ID);
  ID.AddPointer(GV);
  ID.AddInteger(Modifier);
}

void SHConstantPoolGV::print(raw_ostream &O) const {
  O << GV->getName();
  switch (Modifier) {
  case SHCP::GOT:
    O << "@GOT";
    break;
  case SHCP::GOTPC:
    O << "@GOTPC";
    break;
  case SHCP::TPOFF:
    O << "@TPOFF";
    break;
  default:
    break;
  }
}

//===----------------------------------------------------------------------===//
// SHConstantPoolSymbol
//===----------------------------------------------------------------------===//

SHConstantPoolSymbol::SHConstantPoolSymbol(LLVMContext &C, StringRef S,
                                           SHCP::SHCPModifier Mod)
    : SHConstantPoolValue(C, SHCP::CPExtSymbol), Symbol(S), Modifier(Mod) {}

SHConstantPoolSymbol *SHConstantPoolSymbol::Create(LLVMContext &C, StringRef S,
                                                   SHCP::SHCPModifier Mod) {
  return new SHConstantPoolSymbol(C, S, Mod);
}

int SHConstantPoolSymbol::getExistingMachineCPValue(MachineConstantPool *CP,
                                                    Align Alignment) {
  const std::vector<MachineConstantPoolEntry> &Constants = CP->getConstants();
  for (unsigned i = 0, e = Constants.size(); i != e; ++i) {
    if (Constants[i].isMachineConstantPoolEntry() &&
        Constants[i].getAlign() >= Alignment) {
      auto *V =
          static_cast<SHConstantPoolValue *>(Constants[i].Val.MachineCPVal);
      if (auto *SV = dyn_cast<SHConstantPoolSymbol>(V))
        if (SV->Symbol == Symbol && SV->Modifier == Modifier)
          return i;
    }
  }
  return -1;
}

void SHConstantPoolSymbol::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  SHConstantPoolValue::addSelectionDAGCSEId(ID);
  ID.AddString(Symbol);
  ID.AddInteger(Modifier);
}

void SHConstantPoolSymbol::print(raw_ostream &O) const {
  O << Symbol;
  switch (Modifier) {
  case SHCP::GOT:
    O << "@GOT";
    break;
  case SHCP::GOTPC:
    O << "@GOTPC";
    break;
  case SHCP::TPOFF:
    O << "@TPOFF";
    break;
  default:
    break;
  }
}

//===----------------------------------------------------------------------===//
// SHConstantPoolBA
//===----------------------------------------------------------------------===//

SHConstantPoolBA::SHConstantPoolBA(const BlockAddress *BA, int Offset)
    : SHConstantPoolValue(BA->getType(), SHCP::CPBlockAddress), BA(BA),
      Offset(Offset) {}

SHConstantPoolBA *SHConstantPoolBA::Create(const BlockAddress *BA, int Offset) {
  return new SHConstantPoolBA(BA, Offset);
}

int SHConstantPoolBA::getExistingMachineCPValue(MachineConstantPool *CP,
                                                Align Alignment) {
  const std::vector<MachineConstantPoolEntry> &Constants = CP->getConstants();
  for (unsigned i = 0, e = Constants.size(); i != e; ++i) {
    if (Constants[i].isMachineConstantPoolEntry() &&
        Constants[i].getAlign() >= Alignment) {
      auto *V =
          static_cast<SHConstantPoolValue *>(Constants[i].Val.MachineCPVal);
      if (auto *BV = dyn_cast<SHConstantPoolBA>(V))
        if (BV->BA == BA && BV->Offset == Offset)
          return i;
    }
  }
  return -1;
}

void SHConstantPoolBA::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  SHConstantPoolValue::addSelectionDAGCSEId(ID);
  ID.AddPointer(BA);
  ID.AddInteger(Offset);
}

void SHConstantPoolBA::print(raw_ostream &O) const {
  O << "blockaddress";
  if (Offset)
    O << "+" << Offset;
}

//===----------------------------------------------------------------------===//
// SHConstantPoolMBB
//===----------------------------------------------------------------------===//

SHConstantPoolMBB::SHConstantPoolMBB(LLVMContext &C,
                                     const MachineBasicBlock *MBB)
    : SHConstantPoolValue(C, SHCP::CPMachineBasicBlock), MBB(MBB) {}

SHConstantPoolMBB *SHConstantPoolMBB::Create(LLVMContext &C,
                                             const MachineBasicBlock *MBB) {
  return new SHConstantPoolMBB(C, MBB);
}

int SHConstantPoolMBB::getExistingMachineCPValue(MachineConstantPool *CP,
                                                 Align Alignment) {
  const std::vector<MachineConstantPoolEntry> &Constants = CP->getConstants();
  for (unsigned i = 0, e = Constants.size(); i != e; ++i) {
    if (Constants[i].isMachineConstantPoolEntry() &&
        Constants[i].getAlign() >= Alignment) {
      auto *V =
          static_cast<SHConstantPoolValue *>(Constants[i].Val.MachineCPVal);
      if (auto *MV = dyn_cast<SHConstantPoolMBB>(V))
        if (MV->MBB == MBB)
          return i;
    }
  }
  return -1;
}

void SHConstantPoolMBB::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  SHConstantPoolValue::addSelectionDAGCSEId(ID);
  ID.AddPointer(MBB);
}

void SHConstantPoolMBB::print(raw_ostream &O) const {
  O << "MBB#" << MBB->getNumber();
}

//===----------------------------------------------------------------------===//
// SHConstantPoolJTI
//===----------------------------------------------------------------------===//

SHConstantPoolJTI::SHConstantPoolJTI(LLVMContext &C, unsigned Idx)
    : SHConstantPoolValue(C, SHCP::CPJumpTable), JTIdx(Idx) {}

SHConstantPoolJTI *SHConstantPoolJTI::Create(LLVMContext &C, unsigned Idx) {
  return new SHConstantPoolJTI(C, Idx);
}

int SHConstantPoolJTI::getExistingMachineCPValue(MachineConstantPool *CP,
                                                 Align Alignment) {
  const std::vector<MachineConstantPoolEntry> &Constants = CP->getConstants();
  for (unsigned i = 0, e = Constants.size(); i != e; ++i) {
    if (Constants[i].isMachineConstantPoolEntry() &&
        Constants[i].getAlign() >= Alignment) {
      auto *V =
          static_cast<SHConstantPoolValue *>(Constants[i].Val.MachineCPVal);
      if (auto *JV = dyn_cast<SHConstantPoolJTI>(V))
        if (JV->JTIdx == JTIdx)
          return i;
    }
  }
  return -1;
}

void SHConstantPoolJTI::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  SHConstantPoolValue::addSelectionDAGCSEId(ID);
  ID.AddInteger(JTIdx);
}

void SHConstantPoolJTI::print(raw_ostream &O) const { O << "JTI#" << JTIdx; }

//===----------------------------------------------------------------------===//
// SHConstantPoolCPIAddr
//===----------------------------------------------------------------------===//

SHConstantPoolCPIAddr::SHConstantPoolCPIAddr(LLVMContext &C, unsigned Idx)
    : SHConstantPoolValue(C, SHCP::CPCPIAddr), CPIdx(Idx) {}

SHConstantPoolCPIAddr *SHConstantPoolCPIAddr::Create(LLVMContext &C,
                                                     unsigned Idx) {
  return new SHConstantPoolCPIAddr(C, Idx);
}

int SHConstantPoolCPIAddr::getExistingMachineCPValue(MachineConstantPool *CP,
                                                     Align Alignment) {
  const std::vector<MachineConstantPoolEntry> &Constants = CP->getConstants();
  for (unsigned i = 0, e = Constants.size(); i != e; ++i) {
    if (Constants[i].isMachineConstantPoolEntry() &&
        Constants[i].getAlign() >= Alignment) {
      auto *V =
          static_cast<SHConstantPoolValue *>(Constants[i].Val.MachineCPVal);
      if (auto *CV = dyn_cast<SHConstantPoolCPIAddr>(V))
        if (CV->CPIdx == CPIdx)
          return i;
    }
  }
  return -1;
}

void SHConstantPoolCPIAddr::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  SHConstantPoolValue::addSelectionDAGCSEId(ID);
  ID.AddInteger(CPIdx);
}

void SHConstantPoolCPIAddr::print(raw_ostream &O) const {
  O << "CPI#" << CPIdx;
}
