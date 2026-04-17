//===- SHDTCombine.cpp - Combine ADD #-1 + CMP/EQ #0 into DT ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SH peephole pass: combines an ADD #-1,Rn followed by a compare-to-zero
// (TST Rn,Rn or CMP/EQ Rzero,Rn where Rzero==0) into a single DT Rn
// instruction.
//
// DT Rn: Rn -= 1; T = (Rn == 0)
//
// This saves one instruction per loop iteration in countdown loops.
//
//===----------------------------------------------------------------------===//

#include "SH.h"
#include "SHInstrInfo.h"
#include "SHSubtarget.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "sh-dt-combine"

using namespace llvm;

namespace {

class SHDTCombine : public MachineFunctionPass {
public:
  static char ID;
  SHDTCombine() : MachineFunctionPass(ID) {
    initializeSHDTCombinePass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "SH DT Combine"; }

private:
  /// Check if \p Reg is known to hold zero at instruction \p MI.
  /// Scans backward in the same basic block for a `MOV #0, Reg`.
  /// If the register is a live-in that is never redefined in the block,
  /// also checks predecessor blocks for the defining MOV_I8.
  bool isRegKnownZero(MachineInstr &MI, Register Reg) const;
};

} // end anonymous namespace

char SHDTCombine::ID = 0;

INITIALIZE_PASS(SHDTCombine, DEBUG_TYPE, "SH DT Combine", false, false)

bool SHDTCombine::isRegKnownZero(MachineInstr &MI, Register Reg) const {
  MachineBasicBlock *MBB = MI.getParent();

  // Walk backward from MI looking for a def of Reg in this block.
  for (auto I = MachineBasicBlock::reverse_iterator(MI), E = MBB->rend();
       I != E; ++I) {
    if (I->modifiesRegister(Reg, /*TRI=*/nullptr)) {
      if (I->getOpcode() == SH::MOV_I8 && I->getOperand(0).getReg() == Reg &&
          I->getOperand(1).getImm() == 0)
        return true;
      return false;
    }
  }

  // Reg is a live-in to this block — check all predecessors.
  // If a predecessor modifies Reg, it must do so via MOV #0.
  // If a predecessor does NOT modify Reg at all (pass-through), the value
  // is inherited from its own predecessors — this is fine for self-loop
  // edges where the register is invariant in the loop body.
  if (!MBB->isLiveIn(Reg))
    return false;

  bool HasNonPassthroughPred = false;
  for (MachineBasicBlock *Pred : MBB->predecessors()) {
    bool Modified = false;
    bool IsZero = false;
    for (auto I = Pred->rbegin(), E = Pred->rend(); I != E; ++I) {
      if (I->modifiesRegister(Reg, /*TRI=*/nullptr)) {
        Modified = true;
        if (I->getOpcode() == SH::MOV_I8 && I->getOperand(0).getReg() == Reg &&
            I->getOperand(1).getImm() == 0)
          IsZero = true;
        break;
      }
    }
    if (Modified) {
      HasNonPassthroughPred = true;
      if (!IsZero)
        return false;
    }
    // If !Modified, the register passes through — value is preserved.
  }
  // At least one predecessor must actually define it as zero.
  return HasNonPassthroughPred;
}

bool SHDTCombine::runOnMachineFunction(MachineFunction &MF) {
  const SHSubtarget &STI = MF.getSubtarget<SHSubtarget>();
  const SHInstrInfo &TII = *STI.getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (auto MBBI = MBB.begin(), MBBE = MBB.end(); MBBI != MBBE;) {
      MachineInstr &MI = *MBBI;

      // Look for: ADD #-1, Rn
      if (MI.getOpcode() != SH::ADD_I8 || MI.getOperand(2).getImm() != -1) {
        ++MBBI;
        continue;
      }

      Register Rn = MI.getOperand(0).getReg();

      // Look at the next non-debug instruction.
      auto NextI = std::next(MBBI);
      while (NextI != MBBE && NextI->isDebugInstr())
        ++NextI;
      if (NextI == MBBE) {
        ++MBBI;
        continue;
      }

      MachineInstr &NextMI = *NextI;
      bool IsDTCandidate = false;

      // Pattern 1: TST Rn, Rn  (sets T = (Rn & Rn == 0) = (Rn == 0))
      if (NextMI.getOpcode() == SH::TST_RR &&
          NextMI.getOperand(0).getReg() == Rn &&
          NextMI.getOperand(1).getReg() == Rn) {
        IsDTCandidate = true;
      }

      // Pattern 2: CMP/EQ Rn, Rzero  or  CMP/EQ Rzero, Rn
      // where Rzero is known to hold 0.
      if (!IsDTCandidate && NextMI.getOpcode() == SH::CMP_EQ) {
        Register Op0 = NextMI.getOperand(0).getReg();
        Register Op1 = NextMI.getOperand(1).getReg();
        if (Op0 == Rn && isRegKnownZero(NextMI, Op1))
          IsDTCandidate = true;
        else if (Op1 == Rn && isRegKnownZero(NextMI, Op0))
          IsDTCandidate = true;
      }

      // Pattern 3: CMP/EQ #0, R0  where Rn == R0
      if (!IsDTCandidate && NextMI.getOpcode() == SH::CMP_EQ_I8 &&
          NextMI.getOperand(0).getImm() == 0 && Rn == SH::R0) {
        IsDTCandidate = true;
      }

      if (!IsDTCandidate) {
        ++MBBI;
        continue;
      }

      // Verify that the BT/BF following the CMP is the only user of SR.
      // DT sets both Rn and SR, so make sure nothing else between ADD and
      // the branch depends on the old value of Rn besides the CMP.

      LLVM_DEBUG(dbgs() << "SH DT Combine: replacing:\n  " << MI << "  "
                        << NextMI << "  with DT " << printReg(Rn) << "\n");

      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII.get(SH::DT), Rn).addReg(Rn);

      // Remove the old instructions.
      auto RemoveI = MBBI;
      MBBI = std::next(NextI);
      RemoveI->eraseFromParent();
      NextMI.eraseFromParent();

      Changed = true;
    }
  }

  // If we combined any DT instructions, try to remove now-dead MOV #0
  // instructions that were only used by the eliminated CMP_EQ.
  // Single-pass: collect MOV #0 candidates, then scan once for uses.
  if (Changed) {
    SmallVector<MachineInstr *, 4> MovZeroCandidates;
    SmallDenseSet<Register, 4> CandidateRegs;

    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        if (MI.getOpcode() == SH::MOV_I8 && MI.getOperand(1).getImm() == 0) {
          MovZeroCandidates.push_back(&MI);
          CandidateRegs.insert(MI.getOperand(0).getReg());
        }
      }
    }

    // One pass to find which candidate registers are still read.
    SmallDenseSet<Register, 4> UsedRegs;
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isReg() && MO.isUse() && CandidateRegs.count(MO.getReg()))
            UsedRegs.insert(MO.getReg());
        }
      }
    }

    for (MachineInstr *MI : MovZeroCandidates) {
      if (!UsedRegs.count(MI->getOperand(0).getReg()))
        MI->eraseFromParent();
    }
  }

  return Changed;
}

FunctionPass *llvm::createSHDTCombinePass() { return new SHDTCombine(); }
