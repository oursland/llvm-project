//===-- SHDelaySlotFiller.cpp - SH delay slot filler --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a simple local pass that attempts to fill delay slots of branches and
// jumps with useful instructions. If no instructions can be moved into the
// delay slot, then a NOP is placed.
//
//===----------------------------------------------------------------------===//

#include "SH.h"
#include "SHInstrInfo.h"
#include "SHSubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "sh-delay-slot-filler"

namespace {
class SHDelaySlotFiller : public MachineFunctionPass {
public:
  static char ID;
  SHDelaySlotFiller() : MachineFunctionPass(ID) {
    initializeSHDelaySlotFillerPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "SH Delay Slot Filler"; }

  bool runOnMachineBasicBlock(MachineBasicBlock &MBB,
                              const TargetInstrInfo &TII);
  bool runOnMachineFunction(MachineFunction &MF) override {
    bool Changed = false;
    const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

    for (MachineBasicBlock &MBB : MF)
      Changed |= runOnMachineBasicBlock(MBB, TII);

    return Changed;
  }
};
char SHDelaySlotFiller::ID = 0;

} // end of anonymous namespace

INITIALIZE_PASS(SHDelaySlotFiller, DEBUG_TYPE, "SH Delay Slot Filler", false,
                false)

bool SHDelaySlotFiller::runOnMachineBasicBlock(MachineBasicBlock &MBB,
                                               const TargetInstrInfo &TII) {
  bool Changed = false;

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &MI = *I;
    ++I;

    if (!MI.hasDelaySlot())
      continue;

    // We found a delayed instruction! To safely execute it, we must emit a
    // delay slot instruction. In this simple phase 1 implementation, we
    // just emit a NOP explicitly.
    BuildMI(MBB, I, MI.getDebugLoc(), TII.get(SH::NOP));

    // Bundle the NOP with the delayed instruction so they are not separated
    // during layout or branch relaxation. Since I now points to the instruction
    // *after* the newly inserted NOP, we use std::prev(I) as the end iterator.
    MIBundleBuilder(MBB, MachineBasicBlock::iterator(MI), I);

    Changed = true;
  }

  return Changed;
}

FunctionPass *llvm::createSHDelaySlotFillerPass() {
  return new SHDelaySlotFiller();
}
