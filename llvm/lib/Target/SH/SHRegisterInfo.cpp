//===- SHRegisterInfo.cpp - SH Register Information -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH register info interface.
//
//===----------------------------------------------------------------------===//

#include "SHRegisterInfo.h"
#include "SH.h"
#include "SHFrameLowering.h"
#include "SHSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "sh-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "SHGenRegisterInfo.inc"

SHRegisterInfo::SHRegisterInfo() : SHGenRegisterInfo(SH::PR) {}

const MCPhysReg *
SHRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_SH_SaveList;
}

BitVector SHRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  // Stack pointer and status register are always reserved.
  Reserved.set(SH::R15); // SP
  Reserved.set(SH::SR);
  Reserved.set(SH::GBR);
  // Frame pointer if used.
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  if (TFI->hasFP(MF))
    Reserved.set(SH::R14);
  // Base pointer for stack-realigned functions.
  // R13 holds the aligned SP value for stable local variable access.
  if (hasStackRealignment(MF))
    Reserved.set(SH::R13);
  return Reserved;
}

bool SHRegisterInfo::requiresRegisterScavenging(
    const MachineFunction &MF) const {
  return true;
}

bool SHRegisterInfo::requiresFrameIndexScavenging(
    const MachineFunction &MF) const {
  return true;
}

bool SHRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                         int SPAdj, unsigned FIOperandNum,
                                         RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  DebugLoc DL = MI.getDebugLoc();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  const SHSubtarget &STI = MF.getSubtarget<SHSubtarget>();
  const SHInstrInfo &TII = *STI.getInstrInfo();
  const TargetFrameLowering *TFI = STI.getFrameLowering();

  Register FrameReg = getFrameRegister(MF);
  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex);
  if (FIOperandNum + 1 < MI.getNumOperands()) {
    const MachineOperand &MOP = MI.getOperand(FIOperandNum + 1);
    if (MOP.isImm())
      Offset += MOP.getImm();
  }

  bool IsFixed = MF.getFrameInfo().isFixedObjectIndex(FrameIndex);
  bool NeedsRealignment =
      MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
  // Check if this specific object requires alignment beyond ABI guarantee.
  Align ObjAlign =
      IsFixed ? Align(1) : MF.getFrameInfo().getObjectAlign(FrameIndex);
  bool ObjNeedsAlignment = ObjAlign > Align(4);

  if (!TFI->hasFP(MF)) {
    Offset += MF.getFrameInfo().getStackSize();
    // Fixed objects (incoming stack arguments, vararg save areas) have
    // offsets relative to entry_SP.  The prologue pushes PR (4 bytes)
    // BEFORE the StackSize allocation.  Since PR is not handled through
    // PEI's callee-saved mechanism, StackSize does not include it.
    // We must add the PR push size so that fixed object addresses
    // correctly reference positions above the frame.
    if (IsFixed && MF.getFrameInfo().hasCalls())
      Offset += 4; // PR push
  } else if (NeedsRealignment && !IsFixed) {
    // When stack realignment is active, use the base pointer (R13) for
    // non-fixed (local) objects.  R13 is set to the aligned SP ABOVE
    // the StackSize allocation in the prologue:
    //   FP → CSR pushes → align → R13 = SP → SP -= StackSize
    // Objects are at negative offsets from R13:
    //   address = R13 + ObjectOffset  (ObjectOffset is negative)
    FrameReg = SH::R13; // Use base pointer (aligned SP snapshot)
  } else {
    // When using the frame pointer:
    //   FP = entry_SP - ProloguePushSize
    // where ProloguePushSize = bytes pushed before FP is set.
    //   - Non-leaf functions: PR (4 bytes) + R14 (4 bytes) = 8
    //   - Leaf functions:     R14 (4 bytes) only = 4
    //
    // For fixed stack objects (incoming stack arguments, positive offsets from
    // entry SP), we need to add the push size to convert from entry-SP-relative
    // to FP-relative addressing:
    //   entry_SP + X  =  FP + X + ProloguePushSize
    //
    // For non-fixed objects (locals/spills, negative offsets), getObjectOffset
    // already returns FP-relative offsets, so no adjustment is needed.
    if (IsFixed) {
      // R14 is always pushed (4 bytes). PR is pushed only if hasCalls().
      int ProloguePushSize = 4; // R14
      if (MF.getFrameInfo().hasCalls())
        ProloguePushSize += 4; // PR
      Offset += ProloguePushSize;
    }
  }

  // Handle debug values.
  if (MI.isDebugValue()) {
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return false;
  }

  // Handle LEA_FI pseudo instruction.
  if (MI.getOpcode() == SH::LEA_FI) {
    Register DestReg = MI.getOperand(0).getReg();

    // Check if offset fits in an 8-bit immediate.
    if (isInt<8>(Offset)) {
      if (DestReg == FrameReg && Offset == 0) {
        // Redundant LEA
        MBB.erase(II);
        return true;
      }
      if (Offset == 0) {
        BuildMI(MBB, II, DL, TII.get(SH::MOV_RR), DestReg).addReg(FrameReg);
      } else {
        BuildMI(MBB, II, DL, TII.get(SH::MOV_RR), DestReg).addReg(FrameReg);
        BuildMI(MBB, II, DL, TII.get(SH::ADD_I8), DestReg)
            .addReg(DestReg)
            .addImm(Offset);
      }
    } else {
      // Large offset: copy FP to DestReg, then apply offset as
      // multiple ADD_I8 instructions (each handles -128..+127).
      // This avoids constant pool entries entirely.
      BuildMI(MBB, II, DL, TII.get(SH::MOV_RR), DestReg).addReg(FrameReg);
      int Remaining = Offset;
      while (Remaining != 0) {
        int Chunk = std::max(-128, std::min(127, Remaining));
        BuildMI(MBB, II, DL, TII.get(SH::ADD_I8), DestReg)
            .addReg(DestReg)
            .addImm(Chunk);
        Remaining -= Chunk;
      }
    }

    MBB.erase(II);
    return true;
  }

  // Handle zero-offset frame references — just replace FI with FrameReg.
  if (Offset == 0) {
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false);
    if (FIOperandNum + 1 < MI.getNumOperands())
      MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
    return false;
  }

  // Compute address: ScratchReg = FrameReg + Offset
  //
  // Strategy for choosing scratch register:
  //
  // 1. For LOADS: use the instruction's destination register as scratch.
  //    The load will overwrite it anyway, so it's safe.  This avoids
  //    creating virtual registers that the scavenger must resolve.
  //
  // 2. For STORES and other instructions: create a virtual register.
  //    The PEI scavenger resolves it to a physical register using true
  //    liveness information, and spills to a pre-allocated emergency slot
  //    when no register is available.  (Follows the Thumb1 pattern.)
  //
  // This hybrid approach:
  //   - Solves Bug 15 (O3 emergency scratch clobber) via scavenger liveness
  //   - Solves Bug 16 (adjacent FI eliminations) via per-elimination scratch
  //   - Avoids "Incomplete scavenging" by minimizing virtual register count

  Register ScratchReg;
  bool NeedR1Restore = false; // Set when R1 is saved for scratch use
  bool IsLoad = MI.mayLoad() && !MI.mayStore();
  // For FP fill (reload), the destination is an FPR — can't use as GPR scratch.
  // Treat it like a store for scratch register selection purposes.
  if (MI.getOpcode() == SH::FMOV_S_FILL || MI.getOpcode() == SH::FMOV_S_SPILL ||
      MI.getOpcode() == SH::FMOV_D_FILL || MI.getOpcode() == SH::FMOV_D_SPILL)
    IsLoad = false;

  if (IsLoad) {
    // For loads, the destination register is the first operand (def).
    // It's safe to use as scratch since the load will overwrite it.
    ScratchReg = MI.getOperand(0).getReg();
  } else if (RS) {
    // Called from the scavenger's own spill/reload context.  Must NOT
    // create a virtual register (circular dependency) and must NOT call
    // RS->scavengeRegisterBackwards (emergency slots in use).
    //
    // Instead, directly pick a GPR that is not:
    //   (a) used by this instruction's operands,
    //   (b) the frame register.
    // The scavenger's store instructions are simple (value + FI),
    // so there are always free GPRs available.
    SmallSet<Register, 8> Exclude;
    Exclude.insert(FrameReg);
    // R0 is special: used for indexed addressing (@(r0,rn)) and ISel may
    // have assigned it for a long-lived value. Never use it as scratch.
    Exclude.insert(SH::R0);
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.getReg().isPhysical())
        Exclude.insert(MO.getReg());
    }
    // Also exclude registers used by the next instruction, since the
    // scratch is alive until the current instruction executes.
    MachineBasicBlock::iterator Next = std::next(II);
    if (Next != MBB.end()) {
      for (const MachineOperand &MO : Next->operands()) {
        if (MO.isReg() && MO.getReg().isPhysical() && MO.isUse())
          Exclude.insert(MO.getReg());
      }
    }
    for (MCPhysReg Reg : SH::GPR_No_R0RegClass) {
      if (!Exclude.contains(Reg) && !MF.getRegInfo().isReserved(Reg)) {
        ScratchReg = Reg;
        break;
      }
    }
    assert(ScratchReg && "Could not find scratch register for scavenger store");
  } else {
    // Normal PEI processing for stores and other non-load instructions.
    //
    // Problem: We cannot use createVirtualRegister + deferred scavenger
    // resolution here. The scavenger's liveness analysis can incorrectly
    // pick a register that is live (e.g., holding a reloaded value
    // later used by a COPY or SHLD), causing silent data corruption.
    //
    // Solution: Pick a GPR not used by this instruction and wrap it
    // with explicit save/restore via stack push/pop. This is always
    // safe regardless of liveness state. Cost: 2 extra instructions
    // (push + pop) per store frame index elimination.
    SmallSet<Register, 8> UsedRegs;
    UsedRegs.insert(FrameReg);
    UsedRegs.insert(SH::R0); // R0 is special (indexed addressing)
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.getReg().isPhysical())
        UsedRegs.insert(MO.getReg());
    }
    // Pick first available GPR not used by this instruction.
    static const MCPhysReg ScratchCandidates[] = {
        SH::R1, SH::R2, SH::R3, SH::R4, SH::R5, SH::R6, SH::R7};
    ScratchReg = Register();
    for (MCPhysReg Reg : ScratchCandidates) {
      if (!UsedRegs.contains(Reg)) {
        ScratchReg = Reg;
        break;
      }
    }
    assert(ScratchReg && "Could not find scratch register for store FI");
    // Save chosen register by pushing it onto the stack.
    BuildMI(MBB, II, DL, TII.get(SH::MOVL_DEC), SH::R15)
        .addReg(ScratchReg)
        .addReg(SH::R15);
    // We'll restore it after the store instruction completes.
    NeedR1Restore = true;
  }

  // When we pushed a scratch register onto the stack (NeedR1Restore=true),
  // SP is now 4 bytes lower.  If FrameReg is SP (R15), we must compensate
  // the offset so that the final address is relative to the original SP.
  if (NeedR1Restore && FrameReg == SH::R15)
    Offset += 4;

  // Compute address: ScratchReg = FrameReg + Offset
  if (isInt<8>(Offset)) {
    BuildMI(MBB, II, DL, TII.get(SH::MOV_RR), ScratchReg).addReg(FrameReg);
    BuildMI(MBB, II, DL, TII.get(SH::ADD_I8), ScratchReg)
        .addReg(ScratchReg)
        .addImm(Offset);
  } else {
    // Large offset: copy FrameReg to ScratchReg, then apply offset as
    // multiple ADD_I8 instructions (each handles -128..+127).
    BuildMI(MBB, II, DL, TII.get(SH::MOV_RR), ScratchReg).addReg(FrameReg);
    int Remaining = Offset;
    while (Remaining != 0) {
      int Chunk = std::max(-128, std::min(127, Remaining));
      BuildMI(MBB, II, DL, TII.get(SH::ADD_I8), ScratchReg)
          .addReg(ScratchReg)
          .addImm(Chunk);
      Remaining -= Chunk;
    }
  }

  // Handle FP spill/fill pseudo instructions.
  // These need special handling because FMOV_S_ST/LD don't support
  // displacement addressing — we must compute the address first.
  if (MI.getOpcode() == SH::FMOV_S_SPILL || MI.getOpcode() == SH::FMOV_S_FILL) {
    bool IsSpill = (MI.getOpcode() == SH::FMOV_S_SPILL);

    if (IsSpill) {
      // FMOV_S_SPILL: FR, FI, offset → FMOV_S_ST @ScratchReg, FR
      Register FReg = MI.getOperand(0).getReg();
      auto &StoreMI =
          *BuildMI(MBB, II, DL, TII.get(SH::FMOV_S_ST))
               .addReg(ScratchReg)
               .addReg(FReg, getKillRegState(MI.getOperand(0).isKill()));
      if (NeedR1Restore) {
        BuildMI(MBB, std::next(MachineBasicBlock::iterator(StoreMI)), DL,
                TII.get(SH::MOVL_INC))
            .addDef(ScratchReg)
            .addDef(SH::R15)
            .addReg(SH::R15);
      }
    } else {
      // FMOV_S_FILL: FR, FI, offset → FMOV_S_LD @ScratchReg → FR
      Register FReg = MI.getOperand(0).getReg();
      auto &LoadMI = *BuildMI(MBB, II, DL, TII.get(SH::FMOV_S_LD), FReg)
                          .addReg(ScratchReg);
      // FP fills use GPR scratch for address — pop if we pushed it.
      if (NeedR1Restore) {
        BuildMI(MBB, std::next(MachineBasicBlock::iterator(LoadMI)), DL,
                TII.get(SH::MOVL_INC))
            .addDef(ScratchReg)
            .addDef(SH::R15)
            .addReg(SH::R15);
      }
    }
    MBB.erase(II);
    return true;
  }

  // Handle FP64 spill/fill pseudo instructions.
  // Expand to two FMOV_S with correct LE word ordering:
  //   [addr+0] = LSW ↔ FRn+1 (odd register)
  //   [addr+4] = MSW ↔ FRn   (even register)
  if (MI.getOpcode() == SH::FMOV_D_SPILL || MI.getOpcode() == SH::FMOV_D_FILL) {
    // Map DRn → (FRn=MSW, FRn+1=LSW)
    Register DRReg = MI.getOperand(0).getReg();
    unsigned FRHi = 0, FRLo = 0; // FRHi=FRn(MSW), FRLo=FRn+1(LSW)
    switch (DRReg) {
    case SH::DR0:
      FRHi = SH::FR0;
      FRLo = SH::FR1;
      break;
    case SH::DR2:
      FRHi = SH::FR2;
      FRLo = SH::FR3;
      break;
    case SH::DR4:
      FRHi = SH::FR4;
      FRLo = SH::FR5;
      break;
    case SH::DR6:
      FRHi = SH::FR6;
      FRLo = SH::FR7;
      break;
    case SH::DR8:
      FRHi = SH::FR8;
      FRLo = SH::FR9;
      break;
    case SH::DR10:
      FRHi = SH::FR10;
      FRLo = SH::FR11;
      break;
    case SH::DR12:
      FRHi = SH::FR12;
      FRLo = SH::FR13;
      break;
    case SH::DR14:
      FRHi = SH::FR14;
      FRLo = SH::FR15;
      break;
    default:
      llvm_unreachable("Invalid DR register in FMOV_D_SPILL/FILL");
    }

    if (MI.getOpcode() == SH::FMOV_D_SPILL) {
      // Store LSW (FRn+1) at [addr+0], MSW (FRn) at [addr+4]
      BuildMI(MBB, II, DL, TII.get(SH::FMOV_S_ST))
          .addReg(ScratchReg)
          .addReg(FRLo);
      BuildMI(MBB, II, DL, TII.get(SH::ADD_I8), ScratchReg)
          .addReg(ScratchReg)
          .addImm(4);
      BuildMI(MBB, II, DL, TII.get(SH::FMOV_S_ST))
          .addReg(ScratchReg)
          .addReg(FRHi);
      if (NeedR1Restore) {
        BuildMI(MBB, II, DL, TII.get(SH::MOVL_INC))
            .addDef(ScratchReg)
            .addDef(SH::R15)
            .addReg(SH::R15);
      }
    } else {
      // Load LSW from [addr+0] → FRn+1, MSW from [addr+4] → FRn
      BuildMI(MBB, II, DL, TII.get(SH::FMOV_S_LD), FRLo).addReg(ScratchReg);
      BuildMI(MBB, II, DL, TII.get(SH::ADD_I8), ScratchReg)
          .addReg(ScratchReg)
          .addImm(4);
      BuildMI(MBB, II, DL, TII.get(SH::FMOV_S_LD), FRHi).addReg(ScratchReg);
      // FP fills use GPR scratch for address — pop if we pushed it.
      if (NeedR1Restore) {
        BuildMI(MBB, II, DL, TII.get(SH::MOVL_INC))
            .addDef(ScratchReg)
            .addDef(SH::R15)
            .addReg(SH::R15);
      }
    }
    MBB.erase(II);
    return true;
  }

  MI.getOperand(FIOperandNum)
      .ChangeToRegister(ScratchReg, false, false,
                        !IsLoad /* kill for stores */);
  if (FIOperandNum + 1 < MI.getNumOperands())
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);

  // If we saved the scratch register, restore it after the store.
  if (NeedR1Restore) {
    BuildMI(MBB, std::next(II), DL, TII.get(SH::MOVL_INC))
        .addDef(ScratchReg)
        .addDef(SH::R15)
        .addReg(SH::R15);
  }

  return true;
}

Register SHRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  return TFI->hasFP(MF) ? SH::R14 : SH::R15;
}

const uint32_t *SHRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                                     CallingConv::ID CC) const {
  return CSR_SH_RegMask;
}
