//===- SHInstrInfo.cpp - SH Instruction Information -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SH instruction info skeleton. Full implementation added in a later patch.
//
//===----------------------------------------------------------------------===//

#include "SHInstrInfo.h"
#include "SH.h"
#include "SHSubtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "SHGenInstrInfo.inc"

SHInstrInfo::SHInstrInfo(const SHSubtarget &STI)
    : SHGenInstrInfo(STI, RI, SH::ADJCALLSTACKDOWN, SH::ADJCALLSTACKUP, ~0u,
                     ~0u),
      RI() {}

void SHInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I, const DebugLoc &DL,
                              Register DestReg, Register SrcReg, bool KillSrc,
                              bool RenamableDest, bool RenamableSrc) const {
  llvm_unreachable("copyPhysReg not yet implemented");
}

void SHInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  llvm_unreachable("storeRegToStackSlot not yet implemented");
}

void SHInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       Register DestReg, int FrameIndex,
                                       const TargetRegisterClass *RC,
                                       Register VReg, unsigned SubReg,
                                       MachineInstr::MIFlag Flags) const {
  llvm_unreachable("loadRegFromStackSlot not yet implemented");
}

bool SHInstrInfo::analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                                MachineBasicBlock *&FBB,
                                SmallVectorImpl<MachineOperand> &Cond,
                                bool AllowModify) const {
  return true;
}

unsigned SHInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *TBB,
                                   MachineBasicBlock *FBB,
                                   ArrayRef<MachineOperand> Cond,
                                   const DebugLoc &DL, int *BytesAdded) const {
  llvm_unreachable("insertBranch not yet implemented");
}

unsigned SHInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                   int *BytesRemoved) const {
  return 0;
}

MachineBasicBlock *
SHInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  return MI.getOperand(0).getMBB();
}

void SHInstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                       MachineBasicBlock &NewDestBB,
                                       MachineBasicBlock &RestoreBB,
                                       const DebugLoc &DL, int64_t BrOffset,
                                       RegScavenger *RS) const {
  llvm_unreachable("insertIndirectBranch not yet implemented");
}

bool SHInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  return true;
}

bool SHInstrInfo::isReMaterializableImpl(const MachineInstr &MI) const {
  return TargetInstrInfo::isReMaterializableImpl(MI);
}

bool SHInstrInfo::expandPostRAPseudo(MachineInstr &MI) const { return false; }

unsigned SHInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  return 2;
}

bool SHInstrInfo::isLegalToSplitMBBAt(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator MBBI) const {
  return true;
}

bool SHInstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                        int64_t BrOffset) const {
  return true;
}
