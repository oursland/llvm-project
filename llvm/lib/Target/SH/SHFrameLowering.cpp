//===- SHFrameLowering.cpp - SH Frame Information -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stack frame layout (growing downward):
//
//   High address (caller's SP before call)
//   [ arguments passed on stack (if any)  ]
//   [ PR (link register), pushed by prologue ]
//   [ R14 (frame pointer), if used          ]
//   [ callee-saved R8-R13                   ]
//   [ local variables / spill slots         ]
//   Low address  (SP after prologue)
//
//===----------------------------------------------------------------------===//

#include "SHFrameLowering.h"
#include "SH.h"
#include "SHInstrInfo.h"
#include "SHMachineFunctionInfo.h"
#include "SHSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "sh-frame-lowering"

using namespace llvm;

bool SHFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         MF.getFrameInfo().hasVarSizedObjects() ||
         MF.getFrameInfo().isFrameAddressTaken() ||
         RegInfo->hasStackRealignment(MF);
}

/// Emit instructions to load an alignment mask into R1.
/// Alignment masks are always -PowerOf2 (e.g., -64 = 0xFFFFFFC0,
/// -4096 = 0xFFFFF000). MOV #imm8 only handles signed 8-bit range
/// [-128,127], so for larger alignments we use MOV+SHLL8 or MOV+SHLL16.
static void emitAlignMask(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI, DebugLoc DL,
                          const SHInstrInfo &TII, int Mask) {
  if (isInt<8>(Mask)) {
    // Align ≤ 128: fits in MOV #imm8 (sign-extended)
    BuildMI(MBB, MBBI, DL, TII.get(SH::MOV_I8), SH::R1).addImm(Mask);
  } else if (isInt<8>(Mask >> 8) && (Mask & 0xFF) == 0) {
    // Align 256-32768: MOV #(Mask>>8), R1; SHLL8 R1
    BuildMI(MBB, MBBI, DL, TII.get(SH::MOV_I8), SH::R1).addImm(Mask >> 8);
    BuildMI(MBB, MBBI, DL, TII.get(SH::SHLL8), SH::R1).addReg(SH::R1);
  } else if (isInt<8>(Mask >> 16) && (Mask & 0xFFFF) == 0) {
    // Align 65536+: MOV #(Mask>>16), R1; SHLL16 R1
    BuildMI(MBB, MBBI, DL, TII.get(SH::MOV_I8), SH::R1).addImm(Mask >> 16);
    BuildMI(MBB, MBBI, DL, TII.get(SH::SHLL16), SH::R1).addReg(SH::R1);
  } else {
    llvm_unreachable("Alignment mask too large for SH");
  }
}

/// Emit a stack adjustment instruction.
/// ADD #imm8, SP handles ±127 bytes; larger adjustments need a two-step op.
static void emitSPAdjustment(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MBBI,
                             const SHInstrInfo &TII, DebugLoc DL, int Amount) {
  if (Amount == 0)
    return;

  // SH: ADD #imm8,Rn has 8-bit signed range [-128,127].
  // For larger adjustments, we chunk into 127-byte steps.
  while (Amount != 0) {
    int Chunk = (Amount > 0) ? std::min(Amount, 127) : std::max(Amount, -128);
    BuildMI(MBB, MBBI, DL, TII.get(SH::ADD_I8), SH::R15)
        .addReg(SH::R15)
        .addImm(Chunk);
    Amount -= Chunk;
  }
}

void SHFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                           BitVector &SavedRegs,
                                           RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  // Always mark PR as needing to be saved if the function makes calls,
  // because PR holds the return address.
  if (MF.getFrameInfo().hasCalls())
    SavedRegs.set(SH::PR);

  // When stack realignment is needed, R13 is used as a base pointer to
  // hold the aligned SP value.  We must save/restore it because callers
  // may use R13 as a callee-saved GPR for their own live values.
  if (MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF))
    SavedRegs.set(SH::R13);
  if (RS &&
      MF.getSubtarget().getRegisterInfo()->requiresRegisterScavenging(MF)) {
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
    const TargetRegisterClass *RC = &SH::GPRRegClass;
    // Allocate 2 emergency spill slots.  At -O3, adjacent frame index
    // eliminations may need simultaneous scratch registers (the first
    // scratch is still live when the scavenger processes the next FI).
    for (int i = 0; i < 2; ++i) {
      int FI = MF.getFrameInfo().CreateStackObject(
          TRI->getSpillSize(*RC), TRI->getSpillAlign(*RC), false);
      RS->addScavengingFrameIndex(FI);
    }
  }
}

void SHFrameLowering::emitPrologue(MachineFunction &MF,
                                   MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *SHFI = MF.getInfo<SHMachineFunctionInfo>();
  const SHInstrInfo &TII = *MF.getSubtarget<SHSubtarget>().getInstrInfo();
  DebugLoc DL;

  int Offset = 0;

  // 1. Push PR if this function makes calls.
  if (MFI.hasCalls()) {
    BuildMI(MBB, MBBI, DL, TII.get(SH::STS_PR_DEC), SH::R15).addReg(SH::R15);
    Offset -= 4;
    unsigned CFIIndex =
        MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, -Offset));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);

    unsigned PRReg =
        MF.getSubtarget().getRegisterInfo()->getDwarfRegNum(SH::PR, true);
    unsigned CFIIndex2 =
        MF.addFrameInst(MCCFIInstruction::createOffset(nullptr, PRReg, Offset));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex2);

    if (!SHFI->hasPRSpillFI()) {
      int FI = MFI.CreateFixedSpillStackObject(4, -4, true);
      SHFI->setPRSpillFI(FI);
    }
  }

  // 2. Push FP and set FP = SP if frame pointer is used.
  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, DL, TII.get(SH::MOVL_DEC), SH::R15)
        .addReg(SH::R14)
        .addReg(SH::R15);
    Offset -= 4;
    unsigned CFIIndex =
        MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, -Offset));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);

    unsigned FPReg =
        MF.getSubtarget().getRegisterInfo()->getDwarfRegNum(SH::R14, true);
    unsigned CFIIndex2 =
        MF.addFrameInst(MCCFIInstruction::createOffset(nullptr, FPReg, Offset));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex2);

    BuildMI(MBB, MBBI, DL, TII.get(SH::MOV_RR), SH::R14).addReg(SH::R15);

    unsigned CFIIndex3 =
        MF.addFrameInst(MCCFIInstruction::createDefCfaRegister(nullptr, FPReg));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex3);
  }

  // 3. Allocate local stack space.
  //
  // For realigned functions, StackSize allocation is deferred to
  // spillCalleeSavedRegisters() — it happens AFTER CSR pushes, alignment,
  // and R13 = SP.  This ensures R13 is at the top of the local area.
  //
  // For non-realigned functions, allocate StackSize here as normal.
  bool NeedsRealignment =
      MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
  uint64_t StackSize = MFI.getStackSize();
  if (StackSize > 0 && !NeedsRealignment) {
    emitSPAdjustment(MBB, MBBI, TII, DL, -(int)StackSize);
    if (!hasFP(MF)) {
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::cfiDefCfaOffset(nullptr, -Offset + StackSize));
      BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }

  // 3b. Dynamic stack alignment for alignas(>4) objects.
  //
  // When callee-saved registers need to be pushed, alignment is emitted
  // in spillCalleeSavedRegisters() AFTER the pushes.  But when there are
  // no callee-saved registers (e.g., O0 leaf functions), we must align
  // SP here since spillCalleeSavedRegisters() won't be called by PEI.
  if (NeedsRealignment) {
    // Check if spillCalleeSavedRegisters will handle alignment + StackSize.
    const std::vector<CalleeSavedInfo> &CSI_check = MFI.getCalleeSavedInfo();
    bool HasCSRPushes = false;
    for (const CalleeSavedInfo &CS : CSI_check) {
      if (CS.getReg() != SH::PR) {
        HasCSRPushes = true;
        break;
      }
    }
    if (!HasCSRPushes) {
      unsigned MaxAlignVal = MFI.getMaxAlign().value();
      int Mask = -(int)MaxAlignVal;
      BuildMI(MBB, MBBI, DL, TII.get(SH::MOV_RR), SH::R0).addReg(SH::R15);
      emitAlignMask(MBB, MBBI, DL, TII, Mask);
      BuildMI(MBB, MBBI, DL, TII.get(SH::AND_RR), SH::R0)
          .addReg(SH::R0)
          .addReg(SH::R1);
      BuildMI(MBB, MBBI, DL, TII.get(SH::MOV_RR), SH::R15).addReg(SH::R0);
      BuildMI(MBB, MBBI, DL, TII.get(SH::MOV_RR), SH::R13).addReg(SH::R15);
      if (StackSize > 0)
        emitSPAdjustment(MBB, MBBI, TII, DL, -(int)StackSize);
    } else if (StackSize > 0) {
      // spillCalleeSavedRegisters emitted CSR pushes + alignment + R13=SP
      // before MBBI.  StackSize wasn't available at that point (PEI
      // computes it after spillCSR), so emit it here, right after R13=SP.
      // spillCSR inserts CSR pushes + alignment + R13=SP AFTER MBBI,
      // so scan forward from MBBI to find MOV_RR R13, R15.
      auto InsertPt = MBBI;
      for (auto I = MBBI, E = MBB.end(); I != E; ++I) {
        if (I->getOpcode() == SH::MOV_RR &&
            I->getOperand(0).getReg() == SH::R13 &&
            I->getOperand(1).getReg() == SH::R15) {
          InsertPt = std::next(I);
          break;
        }
      }
      emitSPAdjustment(MBB, InsertPt, TII, DL, -(int)StackSize);
    }
  }

  // 4. Emit CFI directives for callee-saved registers pushed by
  //    spillCalleeSavedRegisters().
  //
  //    For realigned functions: CSRs are pushed right after FP=SP (before
  //    StackSize alloc), so they are at CFA - 4*(push_index).
  //
  //    For non-realigned functions: CSRs are pushed after StackSize alloc,
  //    so they are at CFA - StackSize - 4*(push_index).
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  int PushIndex = 0;
  for (const CalleeSavedInfo &CS : CSI) {
    unsigned Reg = CS.getReg();
    if (Reg == SH::PR)
      continue; // PR CFI is already emitted in step 1
    PushIndex++;
    int DwarfReg = TRI->getDwarfRegNum(Reg, true);
    if (DwarfReg >= 0) {
      int CFIOffset;
      if (NeedsRealignment) {
        // CSRs are right below FP (before StackSize alloc).
        CFIOffset = Offset - 4 * PushIndex;
      } else {
        // PEI allocated CSR slots as frame objects within StackSize.
        // Use the actual frame object offset (FP-relative) plus the
        // CFA-to-FP adjustment to get the correct CFA-relative position.
        CFIOffset = MFI.getObjectOffset(CS.getFrameIdx()) + Offset;
      }
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, DwarfReg, CFIOffset));
      BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }
}

void SHFrameLowering::emitEpilogue(MachineFunction &MF,
                                   MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const SHInstrInfo &TII = *MF.getSubtarget<SHSubtarget>().getInstrInfo();
  DebugLoc DL = MBB.findDebugLoc(MBBI);

  uint64_t StackSize = MFI.getStackSize();

  // 1. Restore SP from FP if needed.
  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, DL, TII.get(SH::MOV_RR), SH::R15).addReg(SH::R14);
  } else {
    // Deallocate local stack space.
    if (StackSize > 0)
      emitSPAdjustment(MBB, MBBI, TII, DL, (int)StackSize);
  }

  // 1b. For realigned FP-using functions, emit callee-saved register pops
  // HERE (after SP=FP) rather than in restoreCalleeSavedRegisters.
  // restoreCSR returned true for these, so PEI didn't insert any restores.
  bool IsRealigned =
      MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
  bool NeedsDeferredCSRPops = hasFP(MF) && IsRealigned;
  if (NeedsDeferredCSRPops) {
    // SP is now at FP. The callee-saved registers were pushed via @-r15.
    // CSRs are right below FP (pushed BEFORE StackSize alloc via MOVL_DEC).
    // Save area: FP-4 (first push) to FP-N*4 (last).
    // SP needs to be at FP - N*4.
    const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
    unsigned NumCalleeSaves = 0;
    for (auto it = CSI.rbegin(); it != CSI.rend(); ++it) {
      if (it->getReg() != SH::PR)
        NumCalleeSaves++;
    }
    int TotalAdjust = -(int)(NumCalleeSaves * 4);
    if (TotalAdjust != 0) {
      emitSPAdjustment(MBB, MBBI, TII, DL, TotalAdjust);
    }
    for (auto it = CSI.rbegin(); it != CSI.rend(); ++it) {
      unsigned Reg = it->getReg();
      if (Reg == SH::PR)
        continue;
      if (SH::FPR32RegClass.contains(Reg)) {
        BuildMI(MBB, MBBI, DL, TII.get(SH::FMOV_S_INC))
            .addDef(Reg)
            .addDef(SH::R15)
            .addReg(SH::R15);
      } else {
        BuildMI(MBB, MBBI, DL, TII.get(SH::MOVL_INC))
            .addDef(Reg)
            .addDef(SH::R15)
            .addReg(SH::R15);
      }
    }
    // After pops, SP should be at FP. Restore SP=FP again to be sure.
    BuildMI(MBB, MBBI, DL, TII.get(SH::MOV_RR), SH::R15).addReg(SH::R14);
  }

  // 2. Restore FP if needed.
  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, DL, TII.get(SH::MOVL_INC))
        .addDef(SH::R14)  // Operand 0: $Rn (The loaded value)
        .addDef(SH::R15)  // Operand 1: $Rm_wb (The updated stack pointer)
        .addReg(SH::R15); // Operand 2: $Rm (The source stack pointer)
  }

  // 3. Restore PR if saved.
  if (MFI.hasCalls()) {
    BuildMI(MBB, MBBI, DL, TII.get(SH::LDS_PR_INC), SH::R15).addReg(SH::R15);
  }
}

bool SHFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  bool NeedsRealignment = MFI.getMaxAlign() > Align(4);

  // For NON-realigned functions, let PEI use the default
  // storeRegToStackSlot with frame indices.  This ensures CSR saves
  // go to the PEI-assigned frame slot positions, avoiding the offset
  // mismatch caused by MOVL_DEC push/pop not landing at those positions.
  if (!NeedsRealignment) {
    return false; // PEI handles CSR saves via storeRegToStackSlot
  }

  // For realigned functions, we must push CSRs via MOVL_DEC before
  // aligning SP, since the aligned SP position (and thus frame slot
  // offsets) is not known at this point.
  if (CSI.empty())
    return false;
  const SHInstrInfo &TII = *MF.getSubtarget<SHSubtarget>().getInstrInfo();
  DebugLoc DL = MBB.findDebugLoc(MI);

  for (const CalleeSavedInfo &CS : CSI) {
    unsigned Reg = CS.getReg();
    if (Reg == SH::PR)
      continue; // PR is handled in emitPrologue
    if (SH::FPR32RegClass.contains(Reg)) {
      BuildMI(MBB, MI, DL, TII.get(SH::FMOV_S_DEC), SH::R15)
          .addReg(SH::R15)
          .addReg(Reg, RegState::Kill);
    } else {
      BuildMI(MBB, MI, DL, TII.get(SH::MOVL_DEC), SH::R15)
          .addReg(Reg, RegState::Kill)
          .addReg(SH::R15);
    }
  }

  // Dynamic stack alignment for alignas(>4) objects.
  unsigned MaxAlignVal = MFI.getMaxAlign().value();
  int Mask = -(int)MaxAlignVal;

  BuildMI(MBB, MI, DL, TII.get(SH::MOV_RR), SH::R0).addReg(SH::R15);
  emitAlignMask(MBB, MI, DL, TII, Mask);
  BuildMI(MBB, MI, DL, TII.get(SH::AND_RR), SH::R0)
      .addReg(SH::R0)
      .addReg(SH::R1);
  BuildMI(MBB, MI, DL, TII.get(SH::MOV_RR), SH::R15).addReg(SH::R0);
  BuildMI(MBB, MI, DL, TII.get(SH::MOV_RR), SH::R13).addReg(SH::R15);

  return true;
}

bool SHFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;
  MachineFunction &MF = *MBB.getParent();

  // For FP-using functions with realignment, skip callee-saved register
  // restores here.  They will be emitted in emitEpilogue AFTER SP=FP,
  // which correctly positions SP above the save area.
  bool NeedsRealignment =
      MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
  if (hasFP(MF) && NeedsRealignment) {
    return true; // Handled — pops deferred to emitEpilogue
  }

  // For non-realigned functions, let PEI use the default
  // loadRegFromStackSlot with frame indices.
  return false;
}

MachineBasicBlock::iterator SHFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  const SHInstrInfo &TII = *MF.getSubtarget<SHSubtarget>().getInstrInfo();
  DebugLoc DL = I->getDebugLoc();

  // When the call frame is reserved (included in StackSize), the outgoing
  // argument area is already allocated by the prologue.  The pseudo is a
  // no-op — just erase it.
  //
  // When the call frame is NOT reserved (variable-sized objects prevent
  // reserving a fixed area), dynamically adjust SP.
  if (!hasReservedCallFrame(MF)) {
    int Amt = I->getOperand(0).getImm();
    if (Amt != 0) {
      bool IsDown = I->getOpcode() == SH::ADJCALLSTACKDOWN;
      emitSPAdjustment(MBB, I, TII, DL, IsDown ? -Amt : Amt);
    }
  }
  return MBB.erase(I);
}
