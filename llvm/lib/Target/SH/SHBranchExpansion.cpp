//===-- SHBranchExpansion.cpp - Expand out-of-range branches ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass expands branch instructions whose offsets are too large to fit
// in their immediate fields.  It runs after the delay slot filler (so
// bundles exist and sizes are accurate) and before the constant island pass
// (so CPI placement sees final instruction layout).
//
// Expansions:
//   BT/BF out of range  → inverted condition + BRA (bundle with NOP)
//   BRA   out of range  → BR_FAR pseudo (expanded to inline long branch
//                          by AsmPrinter: push R0, mov.l, jmp, pop R0)
//
//===----------------------------------------------------------------------===//

#include "SH.h"
#include "SHConstantPoolValue.h"
#include "SHInstrInfo.h"
#include "SHSubtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "sh-branch-expansion"

STATISTIC(NumExpandedCondBr, "Number of conditional branches expanded");
STATISTIC(NumExpandedUncondBr, "Number of unconditional branches expanded");

namespace {

struct MBBInfo {
  uint64_t Size = 0;
  uint64_t Offset = 0;
};

class SHBranchExpansion : public MachineFunctionPass {
public:
  static char ID;
  SHBranchExpansion() : MachineFunctionPass(ID) {
    initializeSHBranchExpansionPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "SH Branch Expansion Pass"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

private:
  const SHInstrInfo *TII = nullptr;
  MachineFunction *MF = nullptr;
  SmallVector<MBBInfo, 16> MBBInfos;

  void initMBBInfo();
  int64_t computeOffset(const MachineInstr *Br) const;
  bool expandBranches();
  void expandFarConditionalBr(MachineBasicBlock &MBB, MachineInstr &MI);
  void expandFarUnconditionalBr(MachineBasicBlock &MBB, MachineInstr &MI);
};

} // end anonymous namespace

char SHBranchExpansion::ID = 0;

INITIALIZE_PASS(SHBranchExpansion, DEBUG_TYPE, "SH branch expansion pass",
                false, false)

FunctionPass *llvm::createSHBranchExpansionPass() {
  return new SHBranchExpansion();
}

/// Compute sizes and offsets for all MBBs.
/// Uses MBB.instrs() to correctly account for bundled instructions
/// (delay slot filler has already run).
void SHBranchExpansion::initMBBInfo() {
  MF->RenumberBlocks();
  MBBInfos.clear();
  MBBInfos.resize(MF->size());

  uint64_t Offset = 0;
  for (unsigned I = 0, E = MBBInfos.size(); I < E; ++I) {
    MachineBasicBlock *MBB = MF->getBlockNumbered(I);
    MBBInfos[I].Offset = Offset;
    uint64_t Size = 0;
    // Use instrs() to visit ALL instructions including bundle internals.
    // MIBundleBuilder creates bundles where the first instruction is the
    // head (not a BUNDLE pseudo); the top-level iterator only visits the
    // head, missing the bundled NOP delay slot's 2 bytes.
    for (const MachineInstr &MI : MBB->instrs())
      Size += TII->getInstSizeInBytes(MI);
    MBBInfos[I].Size = Size;
    Offset += Size;
  }
}

/// Compute the signed byte offset from a branch to its target MBB.
int64_t SHBranchExpansion::computeOffset(const MachineInstr *Br) const {
  int ThisMBB = Br->getParent()->getNumber();
  MachineBasicBlock *TargetMBB = TII->getBranchDestBlock(*Br);
  int TargetNum = TargetMBB->getNumber();

  // Offset of the branch instruction within its MBB
  uint64_t BrOffset = MBBInfos[ThisMBB].Offset;
  for (auto I = Br->getParent()->instr_begin(); &*I != Br; ++I)
    BrOffset += TII->getInstSizeInBytes(*I);

  // SH PC-relative branches use PC+4 as base (PC after fetching the
  // branch + delay slot)
  uint64_t PCBase = BrOffset + 4; // branch(2) + delay_slot(2)

  uint64_t TargetOffset = MBBInfos[TargetNum].Offset;
  return static_cast<int64_t>(TargetOffset) - static_cast<int64_t>(PCBase);
}

/// Expand a far BT or BF to a conditional BR_FAR pseudo.
/// The AsmPrinter expands COND_BR_FAR_T/F to:
///   BF/BT .+10 (inverted skip) + push R0 + mov.l @CPI,R0 + jmp @R0 + pop R0
/// This avoids creating a separate MBB for the fall-through path, which
/// can become stale when the constant island pass reorders blocks.
void SHBranchExpansion::expandFarConditionalBr(MachineBasicBlock &MBB,
                                               MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  MachineBasicBlock *TargetBB = TII->getBranchDestBlock(MI);

  unsigned CondFarOpc = (MI.getOpcode() == SH::BT || MI.getOpcode() == SH::BT_S)
                            ? SH::COND_BR_FAR_T
                            : SH::COND_BR_FAR_F;

  // If the BT/BF is bundled (has delay slot NOP), unbundle and erase it.
  if (MI.isBundledWithSucc()) {
    auto II = MI.getIterator();
    ++II;
    if (II != MBB.instr_end() && II->isBundledWithPred()) {
      II->unbundleFromPred();
      II->eraseFromParent();
    }
  }

  // Save the insert position BEFORE erasing the BT/BF.  When block
  // placement adds an explicit BRA after the conditional branch (because
  // the fallthrough successor is no longer the layout successor), the
  // replacement COND_BR_FAR must be inserted at the original BT/BF
  // position — i.e. before the BRA — not appended to the end of the
  // block.  Appending after the BRA would make the COND_BR_FAR
  // unreachable dead code, silently dropping the conditional branch.
  MachineBasicBlock::iterator InsertPt = std::next(MI.getIterator());
  MI.eraseFromParent();

  // Create CPI for target MBB address.
  auto *CPV =
      SHConstantPoolMBB::Create(MF->getFunction().getContext(), TargetBB);
  unsigned CPI = MF->getConstantPool()->getConstantPoolIndex(CPV, Align(4));
  BuildMI(MBB, InsertPt, DL, TII->get(CondFarOpc))
      .addMBB(TargetBB)
      .addConstantPoolIndex(CPI);

  ++NumExpandedCondBr;
}

/// Expand a far BRA to BR_FAR pseudo (expanded by AsmPrinter).
void SHBranchExpansion::expandFarUnconditionalBr(MachineBasicBlock &MBB,
                                                 MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  MachineBasicBlock *TargetBB = TII->getBranchDestBlock(MI);

  // If the BRA is bundled (has a delay slot from DSF), unbundle first
  if (MI.isBundledWithSucc()) {
    // The delay slot instruction follows MI in the bundle.
    // We need to unbundle it and erase it since BR_FAR has its own
    // delay slot handling.
    auto II = MI.getIterator();
    ++II; // the bundled delay slot instruction
    if (II != MBB.instr_end() && II->isBundledWithPred()) {
      II->unbundleFromPred();
      II->eraseFromParent();
    }
  }

  MI.eraseFromParent();

  // BR_FAR: create CPI for target MBB, CI pass places data within reach.
  MachineFunction &MF = *MBB.getParent();
  auto *CPV =
      SHConstantPoolMBB::Create(MF.getFunction().getContext(), TargetBB);
  unsigned CPI = MF.getConstantPool()->getConstantPoolIndex(CPV, Align(4));
  BuildMI(&MBB, DL, TII->get(SH::BR_FAR))
      .addMBB(TargetBB)
      .addConstantPoolIndex(CPI);

  ++NumExpandedUncondBr;
}

/// Scan all branches and expand those that are out of range.
/// Returns true if any expansion was performed.
///
/// IMPORTANT: We expand one branch at a time and rescan, because
/// expandFarConditionalBr may splice instructions (e.g. a BRA) from one MBB
/// to a newly created MBB.  If we collected all branches upfront, the MBB
/// pointers stored for those spliced instructions would become stale,
/// causing subsequent expansions (e.g. expandFarUnconditionalBr) to insert
/// BR_FAR into the wrong basic block.
bool SHBranchExpansion::expandBranches() {
  bool Changed = false;

  // Expand one branch at a time.  After each expansion, rescan from the
  // beginning because block layout and offsets have changed.
  bool ExpandedOne;
  do {
    ExpandedOne = false;
    initMBBInfo(); // Recompute offsets after any prior expansion

    for (MachineBasicBlock &MBB : *MF) {
      for (MachineInstr &MI : MBB) {
        if (!MI.isBranch() || MI.isIndirectBranch())
          continue;

        unsigned Opc = MI.getOpcode();
        int64_t MaxDisp = getMaxBranchDisp(Opc);
        if (MaxDisp == 0)
          continue; // BR_FAR, JMP, etc. — skip

        int64_t Offset = computeOffset(&MI);
        if (Offset > MaxDisp || Offset < -MaxDisp - 2) {
          if (Opc == SH::BT || Opc == SH::BF)
            expandFarConditionalBr(MBB, MI);
          else
            expandFarUnconditionalBr(MBB, MI);
          ExpandedOne = true;
          Changed = true;
          break; // Restart scan — iterators invalidated
        }
      }
      if (ExpandedOne)
        break; // Restart outer loop
    }
  } while (ExpandedOne);

  return Changed;
}

bool SHBranchExpansion::runOnMachineFunction(MachineFunction &Fn) {
  MF = &Fn;
  const SHSubtarget &STI = Fn.getSubtarget<SHSubtarget>();
  TII = STI.getInstrInfo();

  bool EverChanged = false;

  // Expand until convergence — expanding one branch may push others
  // out of range.
  bool Changed;
  do {
    initMBBInfo();
    Changed = expandBranches();
    EverChanged |= Changed;
  } while (Changed);

  return EverChanged;
}
