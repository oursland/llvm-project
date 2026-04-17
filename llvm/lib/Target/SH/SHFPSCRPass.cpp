//===- SHFPSCRPass.cpp - Toggle FPSCR Precision Register -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass scans basic blocks and toggles the SH FPSCR Precision Register
// (PR) bit to switch between single-precision and double-precision arithmetic.
//
//===----------------------------------------------------------------------===//

#include "SH.h"
#include "SHInstrInfo.h"
#include "SHSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "sh-fpscr"

namespace {
class SHFPSCRPass : public MachineFunctionPass {
public:
  static char ID;
  SHFPSCRPass() : MachineFunctionPass(ID) {
    initializeSHFPSCRPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "SH FPSCR Precision Toggle Pass";
  }
};
} // end anonymous namespace

char SHFPSCRPass::ID = 0;

INITIALIZE_PASS(SHFPSCRPass, DEBUG_TYPE, "SH FPSCR Precision Toggle Pass",
                false, false)

FunctionPass *llvm::createSHFPSCRPass() { return new SHFPSCRPass(); }

/// FPSCR default value: PR=1 (double-precision), SZ=0, FR=0.
/// Constructed as MOV #8, R1; SHLL16 R1 → 0x00080000.
static constexpr unsigned FPSCRDefault_PR1_Imm = 8;

/// Emit a sequence to set FPSCR to a given value, saving and restoring R1
/// via the stack: push R1, MOV imm→R1, [SHLL16 R1], LDS R1→FPSCR, pop R1.
/// If \p NeedShift is true, the immediate is shifted left by 16 bits.
static void emitLoadFPSCR(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator InsertPt,
                          const DebugLoc &DL, const SHInstrInfo &TII,
                          unsigned ImmVal, bool NeedShift) {
  BuildMI(MBB, InsertPt, DL, TII.get(SH::MOVL_DEC), SH::R15)
      .addReg(SH::R1)
      .addReg(SH::R15);
  BuildMI(MBB, InsertPt, DL, TII.get(SH::MOV_I8), SH::R1).addImm(ImmVal);
  if (NeedShift)
    BuildMI(MBB, InsertPt, DL, TII.get(SH::SHLL16), SH::R1).addReg(SH::R1);
  BuildMI(MBB, InsertPt, DL, TII.get(SH::LDS_FPSCR)).addReg(SH::R1);
  BuildMI(MBB, InsertPt, DL, TII.get(SH::MOVL_INC), SH::R1)
      .addDef(SH::R15)
      .addReg(SH::R15);
}

/// Emit FPSCR = 0x00080000 (PR=1, SZ=0, FR=0) — the ABI default.
static void emitFPSCRDefaultPR1(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator InsertPt,
                                const DebugLoc &DL, const SHInstrInfo &TII) {
  emitLoadFPSCR(MBB, InsertPt, DL, TII, FPSCRDefault_PR1_Imm,
                /*NeedShift=*/true);
}

/// Emit FPSCR = 0 (PR=0, SZ=0, FR=0) — for single-precision arithmetic.
static void emitFPSCRClearPR0(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator InsertPt,
                              const DebugLoc &DL, const SHInstrInfo &TII) {
  emitLoadFPSCR(MBB, InsertPt, DL, TII, 0, /*NeedShift=*/false);
}

/// Is this a double-precision arithmetic instruction requiring PR=1?
static bool isDoublePRInst(unsigned Opc) {
  switch (Opc) {
  case SH::FADD_D:
  case SH::FSUB_D:
  case SH::FMUL_D:
  case SH::FDIV_D:
  case SH::FCMP_EQ_D:
  case SH::FCMP_GT_D:
  case SH::FABS_D:
  case SH::FNEG_D:
  case SH::FSQRT_D:
  case SH::FLOAT_D:
  case SH::FTRC_D:
  case SH::FCNVDS:
  case SH::FCNVSD:
  // Combined pseudo instructions that inline double-precision ops:
  case SH::FP_EXTEND_F32_F64: // flds + fcnvsd
  case SH::FP_ROUND_F64_F32:  // fcnvds + fsts
  case SH::FP_TO_SINT_F64:    // ftrc + sts
  case SH::SINT_TO_FP_F64:    // lds + float
    return true;
  default:
    return false;
  }
}

/// Is this a single-precision arithmetic instruction requiring PR=0?
static bool isSinglePRInst(unsigned Opc) {
  switch (Opc) {
  case SH::FADD_S:
  case SH::FSUB_S:
  case SH::FMUL_S:
  case SH::FDIV_S:
  case SH::FCMP_EQ_S:
  case SH::FCMP_GT_S:
  case SH::FABS_S:
  case SH::FNEG_S:
  case SH::FSQRT_S:
  case SH::FLOAT_S:
  case SH::FTRC_S:
  case SH::FMAC_S:
  case SH::FMOV_S_LD:
  case SH::FMOV_S_ST:
  case SH::FMOV_S_INC:
  case SH::FMOV_S_DEC:
  case SH::FMOV_S_R0IND:
  case SH::FMOV_S_R0STO:
  case SH::FMOV_SS:
  case SH::FMOV_S_SPILL:
  case SH::FMOV_S_FILL:
  case SH::SINT_TO_FP_F32:
  case SH::FP_TO_SINT_F32:
    return true;
  default:
    return false;
  }
}

/// Process a single basic block, inserting FPSCR toggles as needed.
/// \p NeedsFPSCRGuard indicates the function has calls or single-precision ops.
/// Returns true if any instructions were inserted.
static bool processBlock(MachineBasicBlock &MBB, const SHInstrInfo &TII,
                         bool NeedsFPSCRGuard) {
  bool Changed = false;

  // SH ABI: FPSCR.PR=1 (double-precision) is the default at function
  // entry/exit and across all call boundaries.
  bool PRIsOne = true;

  // Exception landing pads: the C++ exception unwinder (libgcc) may
  // modify FPSCR during stack unwinding—specifically setting FR=1 to
  // swap the FPU register bank.  Reset FPSCR at landing pads so the
  // correct register bank is active.
  if (MBB.isEHPad() && NeedsFPSCRGuard) {
    auto InsertPt = MBB.begin();
    while (InsertPt != MBB.end() &&
           InsertPt->getOpcode() == TargetOpcode::EH_LABEL)
      ++InsertPt;
    DebugLoc IDL;
    if (InsertPt != MBB.end())
      IDL = InsertPt->getDebugLoc();
    emitFPSCRDefaultPR1(MBB, InsertPt, IDL, TII);
    Changed = true;
  }

  for (auto I = MBB.begin(), E = MBB.end(); I != E;) {
    if (I->getOpcode() == SH::JSR || I->getOpcode() == SH::BSR ||
        I->getOpcode() == SH::BSRF) {
      // SH ABI: callees expect FPSCR.PR=1 at entry.
      if (!PRIsOne) {
        emitFPSCRDefaultPR1(MBB, I, I->getDebugLoc(), TII);
        Changed = true;
      }
      // Skip past the call and its delay slot.
      auto InsertPt = std::next(I);
      if (InsertPt != E && InsertPt->getOpcode() == SH::NOP)
        InsertPt = std::next(InsertPt);

      // Any callee might have modified FPSCR (e.g., strtod sets FR=1).
      if (NeedsFPSCRGuard) {
        emitFPSCRDefaultPR1(MBB, InsertPt, I->getDebugLoc(), TII);
        Changed = true;
      }
      PRIsOne = true;
      I = InsertPt;
      continue;
    }

    // Before RTS: restore FPSCR.PR=1 for ABI compliance.
    if (I->getOpcode() == SH::RTS) {
      if (!PRIsOne) {
        emitFPSCRDefaultPR1(MBB, I, I->getDebugLoc(), TII);
        PRIsOne = true;
        Changed = true;
      }
      ++I;
      continue;
    }

    // Double-precision arithmetic: needs PR=1 (already the ABI default).
    if (isDoublePRInst(I->getOpcode())) {
      if (!PRIsOne) {
        emitFPSCRDefaultPR1(MBB, I, I->getDebugLoc(), TII);
        PRIsOne = true;
        Changed = true;
      }
      ++I;
      continue;
    }

    // Single-precision arithmetic: needs PR=0.
    if (PRIsOne && isSinglePRInst(I->getOpcode())) {
      emitFPSCRClearPR0(MBB, I, I->getDebugLoc(), TII);
      PRIsOne = false;
      Changed = true;
    }
    ++I;
  }

  // End of block: maintain invariant that all blocks are entered with PR=1.
  if (!PRIsOne) {
    MachineBasicBlock::iterator Term = MBB.getFirstTerminator();
    DebugLoc IDL;
    if (Term != MBB.end())
      IDL = Term->getDebugLoc();
    emitFPSCRDefaultPR1(MBB, Term, IDL, TII);
    Changed = true;
  }

  return Changed;
}

bool SHFPSCRPass::runOnMachineFunction(MachineFunction &MF) {
  const SHInstrInfo &TII = *MF.getSubtarget<SHSubtarget>().getInstrInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Pre-scan: does this function contain any single-precision arithmetic?
  bool FnHasSinglePrecOps = false;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (isSinglePRInst(MI.getOpcode())) {
        FnHasSinglePrecOps = true;
        break;
      }
    }
    if (FnHasSinglePrecOps)
      break;
  }

  // Even without single-precision ops, callees may modify FPSCR (e.g.,
  // glibc's strtod sets FPSCR.FR=1, swapping the FPU register bank).
  bool FnNeedsFPSCRGuard = FnHasSinglePrecOps || MFI.hasCalls();

  if (!FnNeedsFPSCRGuard)
    return false;

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    Changed |= processBlock(MBB, TII, FnNeedsFPSCRGuard);

  return Changed;
}
