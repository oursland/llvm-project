//===- SHInstrInfo.cpp - SH Instruction Information -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH instruction info interface.
//
//===----------------------------------------------------------------------===//

#include "SHInstrInfo.h"
#include "SH.h"
#include "SHConstantPoolValue.h"
#include "SHSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "sh-instr-info"

#define GET_INSTRINFO_CTOR_DTOR
#include "SHGenInstrInfo.inc"

SHInstrInfo::SHInstrInfo(const SHSubtarget &STI)
    : SHGenInstrInfo(STI, RI, SH::ADJCALLSTACKDOWN, SH::ADJCALLSTACKUP, ~0u,
                     ~0u),
      RI() {}

/// Map a SH CMP_* opcode to its SHCC condition code.
/// Returns true if the CMP requires operand swapping (GT→LT, GE→LE, etc.).
static bool getCCForCmpOpc(unsigned CmpOpc, SHCC::CondCode &CC, bool &Swap) {
  Swap = false;
  switch (CmpOpc) {
  case SH::CMP_EQ:
    CC = SHCC::EQ;
    return true;
  case SH::CMP_GT:
    CC = SHCC::LT;
    Swap = true;
    return true;
  case SH::CMP_GE:
    CC = SHCC::LE;
    Swap = true;
    return true;
  case SH::CMP_HI:
    CC = SHCC::ULT;
    Swap = true;
    return true;
  case SH::CMP_HS:
    CC = SHCC::ULE;
    Swap = true;
    return true;
  default:
    return false;
  }
}

/// Map a SHCC condition code back to a CMP_* opcode.
/// Returns true if the operands must be swapped for emission.
static bool getCmpOpcForCC(SHCC::CondCode CC, unsigned &CmpOpc, bool &Swap) {
  Swap = false;
  switch (CC) {
  case SHCC::EQ:
    CmpOpc = SH::CMP_EQ;
    return true;
  case SHCC::LT:
    CmpOpc = SH::CMP_GT;
    Swap = true;
    return true;
  case SHCC::LE:
    CmpOpc = SH::CMP_GE;
    Swap = true;
    return true;
  case SHCC::ULT:
    CmpOpc = SH::CMP_HI;
    Swap = true;
    return true;
  case SHCC::ULE:
    CmpOpc = SH::CMP_HS;
    Swap = true;
    return true;
  default:
    return false;
  }
}

/// Map DRn register to its sub-register pair (FRn, FRn+1).
/// On SH: DRn = (FRn=MSW, FRn+1=LSW).
static bool getDRSubRegs(unsigned DRReg, unsigned &FRHi, unsigned &FRLo) {
  static const unsigned FRRegs[] = {SH::FR0,  SH::FR1,  SH::FR2,  SH::FR3,
                                    SH::FR4,  SH::FR5,  SH::FR6,  SH::FR7,
                                    SH::FR8,  SH::FR9,  SH::FR10, SH::FR11,
                                    SH::FR12, SH::FR13, SH::FR14, SH::FR15};
  int Idx = -1;
  switch (DRReg) {
  case SH::DR0:
    Idx = 0;
    break;
  case SH::DR2:
    Idx = 2;
    break;
  case SH::DR4:
    Idx = 4;
    break;
  case SH::DR6:
    Idx = 6;
    break;
  case SH::DR8:
    Idx = 8;
    break;
  case SH::DR10:
    Idx = 10;
    break;
  case SH::DR12:
    Idx = 12;
    break;
  case SH::DR14:
    Idx = 14;
    break;
  default:
    return false;
  }
  FRHi = FRRegs[Idx];     // FRn (even) = MSW of DRn
  FRLo = FRRegs[Idx + 1]; // FRn+1 (odd) = LSW of DRn
  return true;
}

/// Try to extract a CMP condition from the instruction before BranchI.
/// On success, populates Cond with [Op1, Op2, EncodedCC] and returns true.
static bool extractCmpCondition(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator BranchI,
                                bool Invert,
                                SmallVectorImpl<MachineOperand> &Cond) {
  if (BranchI == MBB.begin())
    return false;
  MachineBasicBlock::iterator CmpI = std::prev(BranchI);
  SHCC::CondCode CC;
  bool Swap;
  if (!getCCForCmpOpc(CmpI->getOpcode(), CC, Swap))
    return false;
  Register Op1 = CmpI->getOperand(0).getReg();
  Register Op2 = CmpI->getOperand(1).getReg();
  if (Swap)
    std::swap(Op1, Op2);
  unsigned EncodedCC = CC | (Invert ? SHCC::InvertBit : 0);
  Cond.push_back(MachineOperand::CreateReg(Op1, false));
  Cond.push_back(MachineOperand::CreateReg(Op2, false));
  Cond.push_back(MachineOperand::CreateImm(EncodedCC));
  return true;
}

/// Emit a CMP+BT/BF sequence from the 3-operand Cond encoding.
static void emitCmpBranch(const SHInstrInfo &TII, MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator InsertPt,
                          const DebugLoc &DL, MachineBasicBlock *DestBB,
                          ArrayRef<MachineOperand> Cond, int *BytesAdded) {
  Register LHS = Cond[0].getReg();
  Register RHS = Cond[1].getReg();
  unsigned EncodedCC = Cond[2].getImm();
  auto CC = static_cast<SHCC::CondCode>(EncodedCC & SHCC::CCMask);
  bool Invert = (EncodedCC & SHCC::InvertBit) != 0;

  unsigned CmpOpc;
  bool Swap;
  if (!getCmpOpcForCC(CC, CmpOpc, Swap))
    llvm_unreachable("Invalid SHCC in emitCmpBranch");

  Register Op1 = LHS, Op2 = RHS;
  if (Swap)
    std::swap(Op1, Op2);

  BuildMI(MBB, InsertPt, DL, TII.get(CmpOpc)).addReg(Op1).addReg(Op2);
  unsigned BrOpc = Invert ? SH::BF : SH::BT;
  BuildMI(MBB, InsertPt, DL, TII.get(BrOpc)).addMBB(DestBB);
  if (BytesAdded)
    *BytesAdded += 4; // CMP (2 bytes) + BT/BF (2 bytes)
}

void SHInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I, const DebugLoc &DL,
                              Register DestReg, Register SrcReg, bool KillSrc,
                              bool RenamableDest, bool RenamableSrc) const {
  // Dispatch to the correct move instruction based on register class.
  if (SH::FPR32RegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, I, DL, get(SH::FMOV_SS), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  } else if (SH::FPR64RegClass.contains(DestReg, SrcReg)) {
    // Double register copy: expand to two single-register moves.
    // We can't use FMOV_DD because it requires FPSCR.SZ=1, and changing
    // FPSCR invalidates all FP register contents per the SH manual.
    // DRn maps to (FRn=MSW, FRn+1=LSW).
    static const unsigned FRSubRegs[][2] = {
        {SH::FR0, SH::FR1},   // DR0
        {SH::FR2, SH::FR3},   // DR2
        {SH::FR4, SH::FR5},   // DR4
        {SH::FR6, SH::FR7},   // DR6
        {SH::FR8, SH::FR9},   // DR8
        {SH::FR10, SH::FR11}, // DR10
        {SH::FR12, SH::FR13}, // DR12
        {SH::FR14, SH::FR15}, // DR14
    };
    static const unsigned DRRegs[] = {
        SH::DR0, SH::DR2,  SH::DR4,  SH::DR6,
        SH::DR8, SH::DR10, SH::DR12, SH::DR14,
    };
    int SrcIdx = -1, DstIdx = -1;
    for (int i = 0; i < 8; ++i) {
      if (DRRegs[i] == SrcReg)
        SrcIdx = i;
      if (DRRegs[i] == DestReg)
        DstIdx = i;
    }
    assert(SrcIdx >= 0 && DstIdx >= 0 && "Unknown DR register");
    // Copy both halves using single-precision fmov
    BuildMI(MBB, I, DL, get(SH::FMOV_SS), FRSubRegs[DstIdx][0])
        .addReg(FRSubRegs[SrcIdx][0]);
    BuildMI(MBB, I, DL, get(SH::FMOV_SS), FRSubRegs[DstIdx][1])
        .addReg(FRSubRegs[SrcIdx][1], getKillRegState(KillSrc));
  } else {
    BuildMI(MBB, I, DL, get(SH::MOV_RR), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  }
}

void SHInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  DebugLoc DL = MBB.findDebugLoc(MI);

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  // Dispatch to the correct store instruction based on register class.
  if (SH::FPR32RegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, MI, DL, get(SH::FMOV_S_SPILL))
        .addReg(SrcReg, getKillRegState(IsKill))
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addMemOperand(MMO);
  } else if (SH::FPR64RegClass.hasSubClassEq(RC)) {
    // Double-precision: spill via FMOV_D_SPILL pseudo.
    // eliminateFrameIndex expands this into two FMOV_S operations
    // with correct LE word ordering (LSW at lower address).
    BuildMI(MBB, MI, DL, get(SH::FMOV_D_SPILL))
        .addReg(SrcReg, getKillRegState(IsKill))
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addMemOperand(MMO);
  } else {
    BuildMI(MBB, MI, DL, get(SH::MOVL_STO))
        .addReg(SrcReg, getKillRegState(IsKill))
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addMemOperand(MMO);
  }
}

void SHInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       Register DestReg, int FrameIndex,
                                       const TargetRegisterClass *RC,
                                       Register VReg, unsigned SubReg,
                                       MachineInstr::MIFlag Flags) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  DebugLoc DL = MBB.findDebugLoc(MI);

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  // Dispatch to the correct load instruction based on register class.
  if (SH::FPR32RegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, MI, DL, get(SH::FMOV_S_FILL), DestReg)
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addMemOperand(MMO);
  } else if (SH::FPR64RegClass.hasSubClassEq(RC)) {
    // Double-precision: fill via FMOV_D_FILL pseudo.
    BuildMI(MBB, MI, DL, get(SH::FMOV_D_FILL), DestReg)
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addMemOperand(MMO);
  } else {
    BuildMI(MBB, MI, DL, get(SH::MOVL_IND), DestReg)
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addMemOperand(MMO);
  }
}

bool SHInstrInfo::analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                                MachineBasicBlock *&FBB,
                                SmallVectorImpl<MachineOperand> &Cond,
                                bool AllowModify) const {
  TBB = FBB = nullptr;
  Cond.clear();

  // If the block is empty, it just falls through.
  if (MBB.empty())
    return false;

  // Find the last terminator.
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return false;

  // Skip NOPs
  while (I != MBB.begin() && I->getOpcode() == SH::NOP) {
    --I;
  }

  if (!isUnpredicatedTerminator(*I))
    return false;

  // If the last terminator is a return (RTS/RTE/EH_RETURN), we can't analyze
  // the block's branches — it's a return block, not a branch block.
  // Return true (can't analyze); the verifier handles this via isBarrier().
  if (I->isReturn())
    return true;

  // Count the number of terminators.
  MachineBasicBlock::iterator FirstTerm = I;
  while (FirstTerm != MBB.begin() &&
         (isUnpredicatedTerminator(*std::prev(FirstTerm)) ||
          std::prev(FirstTerm)->getOpcode() == SH::NOP)) {
    --FirstTerm;
  }

  // If there is more than one terminator, and they are not all branch
  // instructions, we cannot handle it.
  for (MachineBasicBlock::iterator J = FirstTerm; J != MBB.end(); ++J) {
    if (J->isDebugInstr() || J->getOpcode() == SH::NOP)
      continue;
    if (J->getOpcode() != SH::BRA && J->getOpcode() != SH::BT &&
        J->getOpcode() != SH::BF && J->getOpcode() != SH::BRCC &&
        !J->isReturn())
      return true; // We can't handle it
  }

  // There are two cases we can handle:
  // 1. One terminator (unconditional or conditional)
  // 2. Two terminators (conditional followed by unconditional)

  if (FirstTerm == I) {
    if (I->getOpcode() == SH::BRA) {
      TBB = I->getOperand(0).getMBB();
      return false;
    }
    if (I->getOpcode() == SH::BT || I->getOpcode() == SH::BF) {
      // After expandPostRAPseudo, standalone BT/BF are preceded by a CMP_*.
      // We must capture the CMP operands so the branch folder can correctly
      // re-emit both CMP and BT/BF when manipulating branches.
      bool Invert = (I->getOpcode() == SH::BF);
      TBB = I->getOperand(0).getMBB();

      if (extractCmpCondition(MBB, I, Invert, Cond))
        return false;
      // No preceding CMP — fall through to 1-operand Cond
      Cond.push_back(MachineOperand::CreateImm(I->getOpcode()));
      return false;
    }
    if (I->getOpcode() == SH::BRCC) {
      TBB = I->getOperand(3).getMBB();
      Cond.push_back(I->getOperand(0));
      Cond.push_back(I->getOperand(1));
      Cond.push_back(I->getOperand(2));
      return false;
    }
    return true;
  }

  // Walk backwards from I through all branches to find the conditional branch.
  // Handle patterns like BF+NOP+BRA+NOP or BF+NOP+BRA+NOP+BRA+NOP (redundant
  // BRAs). Use the last BRA as the unconditional branch target and find the
  // conditional branch that precedes it.
  if (I->getOpcode() == SH::BRA) {
    // I is the last BRA. Find the conditional branch.
    MachineBasicBlock::iterator Prev = std::prev(I);
    // Skip NOPs and additional BRAs to find the conditional branch
    while (Prev != MBB.begin() &&
           (Prev->isDebugInstr() || Prev->getOpcode() == SH::NOP ||
            Prev->getOpcode() == SH::BRA)) {
      --Prev;
    }

    if (Prev->getOpcode() == SH::BT || Prev->getOpcode() == SH::BF) {
      bool Invert = (Prev->getOpcode() == SH::BF);
      TBB = Prev->getOperand(0).getMBB();
      FBB = I->getOperand(0).getMBB();

      if (!extractCmpCondition(MBB, Prev, Invert, Cond))
        Cond.push_back(MachineOperand::CreateImm(Prev->getOpcode()));
      return false;
    }
    if (Prev->getOpcode() == SH::BRCC) {
      TBB = Prev->getOperand(3).getMBB();
      Cond.push_back(Prev->getOperand(0));
      Cond.push_back(Prev->getOperand(1));
      Cond.push_back(Prev->getOperand(2));
      FBB = I->getOperand(0).getMBB();
      return false;
    }
  }

  return true;
}

unsigned SHInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *TBB,
                                   MachineBasicBlock *FBB,
                                   ArrayRef<MachineOperand> Cond,
                                   const DebugLoc &DL, int *BytesAdded) const {
  if (Cond.empty()) {
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(SH::BRA)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded = 2;
    return 1;
  }

  if (Cond.size() == 1) {
    unsigned Opc = Cond[0].getImm();
    BuildMI(&MBB, DL, get(Opc)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += 2;
  } else if (Cond.size() == 3) {
    emitCmpBranch(*this, MBB, MBB.end(), DL, TBB, Cond, BytesAdded);
  } else {
    llvm_unreachable("Unknown branch condition!");
  }

  if (FBB) {
    BuildMI(&MBB, DL, get(SH::BRA)).addMBB(FBB);
    if (BytesAdded)
      *BytesAdded += 2;
    return 2;
  }
  return 1;
}

unsigned SHInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                   int *BytesRemoved) const {
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  // Collect instructions to remove by walking backwards from the end.
  // We remove branch terminators (BRA, BT, BF, BRCC, NOP) and any CMP
  // instruction that is immediately paired with a BT/BF (i.e., the CMP
  // that sets SR for the branch).  We must NOT remove CMP instructions
  // that are used for value-computation (e.g., CMP+MOVT patterns) and
  // merely happen to be adjacent to a branch sequence.
  SmallVector<MachineInstr *, 8> ToRemove;

  auto isBranchOrNop = [](unsigned Opc) {
    return Opc == SH::BRA || Opc == SH::BT || Opc == SH::BF ||
           Opc == SH::BRCC || Opc == SH::NOP;
  };
  auto isCmp = [](unsigned Opc) {
    return Opc == SH::CMP_EQ || Opc == SH::CMP_GT || Opc == SH::CMP_GE ||
           Opc == SH::CMP_HI || Opc == SH::CMP_HS;
  };

  // Phase 1: Walk backwards and collect all branch/nop terminators.
  while (isBranchOrNop(I->getOpcode())) {
    ToRemove.push_back(&*I);
    if (I == MBB.begin())
      break;
    --I;
  }

  // Phase 2: For a remaining CMP immediately before the first collected
  // branch, remove it only if it is paired with a BT/BF (i.e., it is the
  // SR-setting instruction for the conditional branch).  We must stop
  // after removing at most one CMP to avoid consuming value-producing CMPs.
  if (!ToRemove.empty() && I != MBB.end() && isCmp(I->getOpcode())) {
    // Verify that a BT or BF exists in the collected set — the CMP is
    // only a branch-related instruction if it feeds a conditional branch.
    bool HasConditional = false;
    for (MachineInstr *MI : ToRemove) {
      if (MI->getOpcode() == SH::BT || MI->getOpcode() == SH::BF) {
        HasConditional = true;
        break;
      }
    }
    if (HasConditional)
      ToRemove.push_back(&*I);
  }

  // Phase 3: Erase collected instructions.
  unsigned Count = 0;
  for (MachineInstr *MI : ToRemove) {
    if (BytesRemoved)
      *BytesRemoved += getInstSizeInBytes(*MI);
    MI->eraseFromParent();
    ++Count;
  }
  return Count;
}

MachineBasicBlock *
SHInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unexpected machine instruction in getBranchDestBlock");
  case SH::BRA:
  case SH::BF:
  case SH::BT:
    return MI.getOperand(0).getMBB();
  case SH::BRCC:
    return MI.getOperand(3).getMBB();
  }
}

void SHInstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                       MachineBasicBlock &NewDestBB,
                                       MachineBasicBlock &RestoreBB,
                                       const DebugLoc &DL, int64_t BrOffset,
                                       RegScavenger *RS) const {
  // Use BR_FAR pseudo which the AsmPrinter expands to:
  //   push R0; mov.l @CPI, R0; jmp @R0; pop R0
  // CPI points to SHConstantPoolMBB containing the target MBB address.
  MachineFunction &MF = *MBB.getParent();
  auto *CPV =
      SHConstantPoolMBB::Create(MF.getFunction().getContext(), &NewDestBB);
  unsigned CPI = MF.getConstantPool()->getConstantPoolIndex(CPV, Align(4));
  BuildMI(MBB, MBB.end(), DL, get(SH::BR_FAR))
      .addMBB(&NewDestBB)
      .addConstantPoolIndex(CPI);
}

bool SHInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond.size() == 1) {
    unsigned Opc = Cond[0].getImm();
    if (Opc == SH::BT)
      Cond[0].setImm(SH::BF);
    else if (Opc == SH::BF)
      Cond[0].setImm(SH::BT);
    else
      return true;
    return false;
  } else if (Cond.size() == 3) {
    unsigned EncodedCC = Cond[2].getImm();
    Cond[2].setImm(EncodedCC ^ SHCC::InvertBit);
    return false;
  }
  return true;
}

unsigned SHInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  if (MI.isInlineAsm()) {
    const MachineFunction *MF = MI.getParent()->getParent();
    const char *AsmStr = MI.getOperand(0).getSymbolName();
    return getInlineAsmLength(AsmStr, *MF->getTarget().getMCAsmInfo());
  }
  if (MI.getOpcode() == SH::MOV_I32)
    // Always CPI: mov.l = 2 bytes (JTI rewritten to CPI by CI pass).
    return 2;
  if (MI.getOpcode() == SH::BR_FAR)
    return 8; // push(2) + mov.l(2) + jmp(2) + pop(2) = 8 (CPI data placed by CI
              // pass)
  if (MI.getOpcode() == SH::COND_BR_FAR_T ||
      MI.getOpcode() == SH::COND_BR_FAR_F)
    return 10; // BF/BT(2) + push(2) + mov.l(2) + jmp(2) + pop(2) = 10
  // For BUNDLE pseudo: sum the sizes of all bundled instructions.
  // This allows callers (CI pass, BranchExpansion) to use the standard
  // MBB-level iterator and get correct sizes without special bundle handling.
  if (MI.getOpcode() == TargetOpcode::BUNDLE) {
    unsigned Size = 0;
    auto I = MI.getIterator();
    ++I; // skip bundle header
    while (I->isBundledWithPred()) {
      Size += getInstSizeInBytes(*I);
      ++I;
    }
    return Size;
  }
  // Pseudos expanded by AsmPrinter to real instructions.
  // Sizes must match the AsmPrinter expansion exactly so that
  // CI pass's computeBlockSize agrees with the MC layout.
  switch (MI.getOpcode()) {
  // 2-instruction expansions: op + sts/lds (4 bytes each)
  case SH::MUL32_PSEUDO:
  case SH::MULHS_PSEUDO:
  case SH::MULHU_PSEUDO:
  case SH::FP_TO_SINT_F32:
  case SH::FP_TO_SINT_F64:
  case SH::SINT_TO_FP_F32:
  case SH::SINT_TO_FP_F64:
  case SH::FP_EXTEND_F32_F64:
  case SH::FP_ROUND_F64_F32:
  case SH::BITCAST_F32_TO_I32:
  case SH::BITCAST_I32_TO_F32:
  case SH::FMOV_D_INC:
  case SH::FMOV_D_DEC:
    return 4;
  case SH::FMOV_D_LD:
    return 6;
  case SH::FMOV_D_ST:
  case SH::FMOV_D_R0IND:
  case SH::FMOV_D_R0STO:
    return 8;
  // mova(2) + mov.l(2) + add(2) = 6 bytes (CPI data placed by CI pass)
  case SH::MOV_GOT:
    return 6;
  default:
    break;
  }
  if (MI.isPseudo())
    return 0;
  return 2;
}

bool SHInstrInfo::isLegalToSplitMBBAt(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator MBBI) const {
  // MOVT reads the T-bit (SR register) which was set by the preceding
  // CMP/TST instruction.  SR is a reserved register whose liveness is NOT
  // tracked in basic-block live-in lists.  If the branch-folder's tail-merge
  // splits a block at MOVT, the new block has two predecessors: one that
  // sets SR correctly (via CMP/TST) and another that does NOT.  The MOVT
  // at the top of the merged block then reads an undefined/stale SR value
  // from the second predecessor, producing incorrect results.
  //
  // Similarly, any instruction that reads SR (BT, BF) should not be the
  // start of a split block.
  if (MBBI != MBB.end()) {
    unsigned Opc = MBBI->getOpcode();
    if (Opc == SH::MOVT || Opc == SH::BT || Opc == SH::BF)
      return false;
  }
  return true;
}

bool SHInstrInfo::isReMaterializableImpl(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case SH::MOV_I32:
  case SH::CPI_ADDR:
    // These pseudos load constants from the constant pool via PC-relative
    // addressing.  They are pure value producers with no side effects.
    // The default implementation rejects them because they have CPI
    // operands, but they are trivially rematerializable — the constant
    // island pass will place the pool entry within range of any use.
    return true;
  default:
    return TargetInstrInfo::isReMaterializableImpl(MI);
  }
}

/// Expand FMOV_D pseudo-instructions into pairs of single-precision FMOV
/// operations. Returns true if MI was expanded and erased.
///
/// In little-endian memory: [addr+0]=LSW, [addr+4]=MSW.
/// So: FRHi (FRn, even) = MSW at higher address
///     FRLo (FRn+1, odd) = LSW at lower address
static bool expandFMOVDouble(MachineInstr &MI, const SHInstrInfo &TII) {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  unsigned Opc = MI.getOpcode();

  // FMOV_D_LD: fmov @Rm, DRn → load LSW then MSW
  if (Opc == SH::FMOV_D_LD) {
    unsigned DRReg = MI.getOperand(0).getReg();
    unsigned AddrReg = MI.getOperand(1).getReg();
    unsigned FRHi, FRLo;
    if (!getDRSubRegs(DRReg, FRHi, FRLo))
      return false;
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_LD), FRLo).addReg(AddrReg);
    BuildMI(MBB, MI, DL, TII.get(SH::ADD_I8), AddrReg)
        .addReg(AddrReg)
        .addImm(4);
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_LD), FRHi).addReg(AddrReg);
    BuildMI(MBB, MI, DL, TII.get(SH::ADD_I8), AddrReg)
        .addReg(AddrReg)
        .addImm(-4);
    MI.eraseFromParent();
    return true;
  }

  // FMOV_D_ST: fmov DRm, @Rn → store LSW then MSW
  if (Opc == SH::FMOV_D_ST) {
    unsigned AddrReg = MI.getOperand(0).getReg();
    unsigned DRReg = MI.getOperand(1).getReg();
    unsigned FRHi, FRLo;
    if (!getDRSubRegs(DRReg, FRHi, FRLo))
      return false;
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_ST)).addReg(AddrReg).addReg(FRLo);
    BuildMI(MBB, MI, DL, TII.get(SH::ADD_I8), AddrReg)
        .addReg(AddrReg)
        .addImm(4);
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_ST)).addReg(AddrReg).addReg(FRHi);
    BuildMI(MBB, MI, DL, TII.get(SH::ADD_I8), AddrReg)
        .addReg(AddrReg)
        .addImm(-4);
    MI.eraseFromParent();
    return true;
  }

  // FMOV_D_INC: fmov @Rm+, DRn → post-increment load LSW then MSW
  if (Opc == SH::FMOV_D_INC) {
    unsigned DRReg = MI.getOperand(0).getReg();
    unsigned AddrWB = MI.getOperand(1).getReg();
    unsigned AddrReg = MI.getOperand(2).getReg();
    unsigned FRHi, FRLo;
    if (!getDRSubRegs(DRReg, FRHi, FRLo))
      return false;
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_INC), FRLo)
        .addDef(AddrWB)
        .addReg(AddrReg);
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_INC), FRHi)
        .addDef(AddrWB)
        .addReg(AddrWB);
    MI.eraseFromParent();
    return true;
  }

  // FMOV_D_DEC: fmov DRm, @-Rn → pre-decrement store MSW then LSW
  if (Opc == SH::FMOV_D_DEC) {
    unsigned AddrWB = MI.getOperand(0).getReg();
    unsigned AddrReg = MI.getOperand(1).getReg();
    unsigned DRReg = MI.getOperand(2).getReg();
    unsigned FRHi, FRLo;
    if (!getDRSubRegs(DRReg, FRHi, FRLo))
      return false;
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_DEC))
        .addDef(AddrWB)
        .addReg(AddrReg)
        .addReg(FRHi);
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_DEC))
        .addDef(AddrWB)
        .addReg(AddrWB)
        .addReg(FRLo);
    MI.eraseFromParent();
    return true;
  }

  // FMOV_D_R0IND: fmov @(R0,Rm), DRn → R0-indexed load
  if (Opc == SH::FMOV_D_R0IND) {
    unsigned DRReg = MI.getOperand(0).getReg();
    unsigned AddrReg = MI.getOperand(1).getReg();
    unsigned FRHi, FRLo;
    if (!getDRSubRegs(DRReg, FRHi, FRLo))
      return false;
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_R0IND), FRLo).addReg(AddrReg);
    BuildMI(MBB, MI, DL, TII.get(SH::ADD_I8), SH::R0).addReg(SH::R0).addImm(4);
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_R0IND), FRHi).addReg(AddrReg);
    BuildMI(MBB, MI, DL, TII.get(SH::ADD_I8), SH::R0).addReg(SH::R0).addImm(-4);
    MI.eraseFromParent();
    return true;
  }

  // FMOV_D_R0STO: fmov DRm, @(R0,Rn) → R0-indexed store
  if (Opc == SH::FMOV_D_R0STO) {
    unsigned AddrReg = MI.getOperand(0).getReg();
    unsigned DRReg = MI.getOperand(1).getReg();
    unsigned FRHi, FRLo;
    if (!getDRSubRegs(DRReg, FRHi, FRLo))
      return false;
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_R0STO))
        .addReg(AddrReg)
        .addReg(FRLo);
    BuildMI(MBB, MI, DL, TII.get(SH::ADD_I8), SH::R0).addReg(SH::R0).addImm(4);
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_R0STO))
        .addReg(AddrReg)
        .addReg(FRHi);
    BuildMI(MBB, MI, DL, TII.get(SH::ADD_I8), SH::R0).addReg(SH::R0).addImm(-4);
    MI.eraseFromParent();
    return true;
  }

  // FMOV_DD: fmov DRm, DRn → register-to-register double move
  if (Opc == SH::FMOV_DD) {
    unsigned DstDR = MI.getOperand(0).getReg();
    unsigned SrcDR = MI.getOperand(1).getReg();
    unsigned DstHi, DstLo, SrcHi, SrcLo;
    if (!getDRSubRegs(DstDR, DstHi, DstLo) ||
        !getDRSubRegs(SrcDR, SrcHi, SrcLo))
      return false;
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_SS), DstHi).addReg(SrcHi);
    BuildMI(MBB, MI, DL, TII.get(SH::FMOV_SS), DstLo).addReg(SrcLo);
    MI.eraseFromParent();
    return true;
  }

  return false;
}

bool SHInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();

  if (MI.getOpcode() == SH::BRCC) {
    Register LHS = MI.getOperand(0).getReg();
    Register RHS = MI.getOperand(1).getReg();
    unsigned EncodedCC = MI.getOperand(2).getImm();
    MachineBasicBlock *DestBB = MI.getOperand(3).getMBB();

    auto CC = static_cast<SHCC::CondCode>(EncodedCC & SHCC::CCMask);
    bool Invert = (EncodedCC & SHCC::InvertBit) != 0;

    unsigned CmpOpc;
    bool Swap;
    if (!getCmpOpcForCC(CC, CmpOpc, Swap))
      llvm_unreachable("Invalid SHCC in BRCC");

    Register Op1 = LHS, Op2 = RHS;
    if (Swap)
      std::swap(Op1, Op2);

    BuildMI(MBB, MI, DL, get(CmpOpc)).addReg(Op1).addReg(Op2);
    unsigned BrOpc = Invert ? SH::BF : SH::BT;
    BuildMI(MBB, MI, DL, get(BrOpc)).addMBB(DestBB);

    MI.eraseFromParent();
    return true;
  }

  if (expandFMOVDouble(MI, *this))
    return true;

  // SINT_TO_FP_F32: int→float conversion via FPUL.
  if (MI.getOpcode() == SH::SINT_TO_FP_F32) {
    Register DstFR = MI.getOperand(0).getReg();
    Register SrcGPR = MI.getOperand(1).getReg();

    BuildMI(MBB, MI, DL, get(SH::LDS_FPUL)).addReg(SrcGPR);
    BuildMI(MBB, MI, DL, get(SH::FLOAT_S), DstFR);

    MI.eraseFromParent();
    return true;
  }

  // FP_TO_SINT_F32: float→int conversion via FPUL.
  if (MI.getOpcode() == SH::FP_TO_SINT_F32) {
    Register DstGPR = MI.getOperand(0).getReg();
    Register SrcFR = MI.getOperand(1).getReg();

    BuildMI(MBB, MI, DL, get(SH::FTRC_S)).addReg(SrcFR);
    BuildMI(MBB, MI, DL, get(SH::STS_FPUL), DstGPR);

    MI.eraseFromParent();
    return true;
  }

  return false;
}

bool SHInstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                        int64_t BrOffset) const {
  switch (BranchOpc) {
  default:
    llvm_unreachable("Unexpected opcode!");
  case SH::BT:
  case SH::BF: {
    // 8-bit displacement. The displacement is added to PC+4.
    // The range of disp is [-128, 127].
    // Target = (Disp * 2) + PC, where PC = InstrAddr + 4
    // So BrOffset = Target - InstrAddr = (Disp * 2) + 4
    // Disp = (BrOffset - 4) / 2
    // We need -128 <= Disp <= 127
    int64_t Disp = (BrOffset - 4) / 2;
    return Disp >= -128 && Disp <= 127;
  }
  case TargetOpcode::INLINEASM:
  case TargetOpcode::INLINEASM_BR:
    return true; // Assume inline asm can reach.
  case SH::BRA:
  case SH::BSR: {
    // 12-bit displacement.
    // Disp = (BrOffset - 4) / 2
    // We need -2048 <= Disp <= 2047
    int64_t Disp = (BrOffset - 4) / 2;
    return Disp >= -2048 && Disp <= 2047;
  }
  }
}
