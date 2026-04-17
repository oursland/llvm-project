//===- SHISelLowering.cpp - SH DAG Lowering Implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SH DAG lowering skeleton. Full implementation added in a later patch.
//
//===----------------------------------------------------------------------===//

#include "SHISelLowering.h"
#include "SHSubtarget.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

using namespace llvm;

SHTargetLowering::SHTargetLowering(const TargetMachine &TM,
                                   const SHSubtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  // Minimal register classes — filled in a later patch.
  computeRegisterProperties(STI.getRegisterInfo());
}

SDValue SHTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  return SDValue();
}

MachineBasicBlock *
SHTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                              MachineBasicBlock *MBB) const {
  return MBB;
}

const char *SHTargetLowering::getTargetNodeName(unsigned Opcode) const {
  return nullptr;
}

bool SHTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                             const AddrMode &AM, Type *Ty,
                                             unsigned AS,
                                             Instruction *I) const {
  return false;
}

bool SHTargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                  SDValue &Base,
                                                  SDValue &Offset,
                                                  ISD::MemIndexedMode &AM,
                                                  SelectionDAG &DAG) const {
  return false;
}

bool SHTargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                                 SDValue &Offset,
                                                 ISD::MemIndexedMode &AM,
                                                 SelectionDAG &DAG) const {
  return false;
}

TargetLowering::ConstraintType
SHTargetLowering::getConstraintType(StringRef Constraint) const {
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
SHTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                               StringRef Constraint,
                                               MVT VT) const {
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

SDValue SHTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  return Chain;
}

SDValue SHTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                    SmallVectorImpl<SDValue> &InVals) const {
  llvm_unreachable("LowerCall not yet implemented");
}

bool SHTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  return true;
}

SDValue
SHTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                              bool isVarArg,
                              const SmallVectorImpl<ISD::OutputArg> &Outs,
                              const SmallVectorImpl<SDValue> &OutVals,
                              const SDLoc &DL, SelectionDAG &DAG) const {
  return Chain;
}

Register SHTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  return Register();
}

Register SHTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  return Register();
}

bool SHTargetLowering::allowsMisalignedMemoryAccesses(
    EVT VT, unsigned AddrSpace, Align Alignment, MachineMemOperand::Flags Flags,
    unsigned *Fast) const {
  return false;
}
