//===- SHAsmPrinter.cpp - SH LLVM Assembly Printer ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH assembly printer pass.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SHInstPrinter.h"
#include "MCTargetDesc/SHMCAsmInfo.h"
#include "MCTargetDesc/SHMCTargetDesc.h"
#include "SH.h"
#include "SHConstantPoolValue.h"
#include "SHMCInstLower.h"
#include "SHSubtarget.h"
#include "SHTargetMachine.h"
#include "TargetInfo/SHTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "sh-asm-printer"

namespace {

class SHAsmPrinter : public AsmPrinter {
public:
  explicit SHAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {}

  StringRef getPassName() const override { return "SH Assembly Printer"; }

  // CI pass handles all constant pool placement via CONSTPOOL_ENTRY pseudo
  // instructions near users.  Suppress the base class emitter which would
  // emit all entries at the top of the function (wrong location for SH's
  // tiny PC-relative displacements).
  void emitConstantPool() override {}

  void emitMachineConstantPoolValue(MachineConstantPoolValue *MCPV) override;

  void emitInstruction(const MachineInstr *MI) override;

  bool isBlockOnlyReachableByFallthrough(
      const MachineBasicBlock *MBB) const override {
    return false;
  }

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;

  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;

private:
  /// Emit a double-precision FMOV pseudo as a pair of single-precision ops.
  void emitFMOVDouble(const MachineInstr &MI);

  /// Emit a far branch pseudo (BR_FAR, COND_BR_FAR_T/F).
  void emitBranchFar(const MachineInstr &MI);
};

} // end anonymous namespace

/// Map a DR register to its sub-register pair (FRHi=MSW, FRLo=LSW).
static void getDRSubRegsForAsm(Register DRReg, unsigned &FRHi, unsigned &FRLo) {
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
    llvm_unreachable("Invalid DR register in FMOV_D expansion");
  }
}

void SHAsmPrinter::emitFMOVDouble(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  bool IsLoad = (Opc == SH::FMOV_D_LD || Opc == SH::FMOV_D_INC ||
                 Opc == SH::FMOV_D_R0IND);

  Register DRReg, AddrReg;
  if (Opc == SH::FMOV_D_INC) {
    DRReg = MI.getOperand(0).getReg();
    AddrReg = MI.getOperand(2).getReg();
  } else if (Opc == SH::FMOV_D_DEC) {
    DRReg = MI.getOperand(2).getReg();
    AddrReg = MI.getOperand(1).getReg();
  } else if (IsLoad) {
    DRReg = MI.getOperand(0).getReg();
    AddrReg = MI.getOperand(1).getReg();
  } else {
    DRReg = MI.getOperand(1).getReg();
    AddrReg = MI.getOperand(0).getReg();
  }

  unsigned FRHi = 0, FRLo = 0;
  getDRSubRegsForAsm(DRReg, FRHi, FRLo);

  auto EmitAdd = [&](Register Reg, int Imm) {
    MCInst Add;
    Add.setOpcode(SH::ADD_I8);
    Add.addOperand(MCOperand::createReg(Reg));
    Add.addOperand(MCOperand::createReg(Reg));
    Add.addOperand(MCOperand::createImm(Imm));
    EmitToStreamer(*OutStreamer, Add);
  };

  if (Opc == SH::FMOV_D_INC) {
    MCInst M1;
    M1.setOpcode(SH::FMOV_S_INC);
    M1.addOperand(MCOperand::createReg(FRLo));
    M1.addOperand(MCOperand::createReg(AddrReg));
    M1.addOperand(MCOperand::createReg(AddrReg));
    EmitToStreamer(*OutStreamer, M1);
    MCInst M2;
    M2.setOpcode(SH::FMOV_S_INC);
    M2.addOperand(MCOperand::createReg(FRHi));
    M2.addOperand(MCOperand::createReg(AddrReg));
    M2.addOperand(MCOperand::createReg(AddrReg));
    EmitToStreamer(*OutStreamer, M2);
  } else if (Opc == SH::FMOV_D_DEC) {
    MCInst M1;
    M1.setOpcode(SH::FMOV_S_DEC);
    M1.addOperand(MCOperand::createReg(AddrReg));
    M1.addOperand(MCOperand::createReg(AddrReg));
    M1.addOperand(MCOperand::createReg(FRHi));
    EmitToStreamer(*OutStreamer, M1);
    MCInst M2;
    M2.setOpcode(SH::FMOV_S_DEC);
    M2.addOperand(MCOperand::createReg(AddrReg));
    M2.addOperand(MCOperand::createReg(AddrReg));
    M2.addOperand(MCOperand::createReg(FRLo));
    EmitToStreamer(*OutStreamer, M2);
  } else if (Opc == SH::FMOV_D_LD) {
    MCInst M1;
    M1.setOpcode(SH::FMOV_S_INC);
    M1.addOperand(MCOperand::createReg(FRLo));
    M1.addOperand(MCOperand::createReg(AddrReg));
    M1.addOperand(MCOperand::createReg(AddrReg));
    EmitToStreamer(*OutStreamer, M1);
    MCInst M2;
    M2.setOpcode(SH::FMOV_S_LD);
    M2.addOperand(MCOperand::createReg(FRHi));
    M2.addOperand(MCOperand::createReg(AddrReg));
    EmitToStreamer(*OutStreamer, M2);
    EmitAdd(AddrReg, -4);
  } else if (Opc == SH::FMOV_D_ST) {
    MCInst M1;
    M1.setOpcode(SH::FMOV_S_ST);
    M1.addOperand(MCOperand::createReg(AddrReg));
    M1.addOperand(MCOperand::createReg(FRLo));
    EmitToStreamer(*OutStreamer, M1);
    EmitAdd(AddrReg, 4);
    MCInst M2;
    M2.setOpcode(SH::FMOV_S_ST);
    M2.addOperand(MCOperand::createReg(AddrReg));
    M2.addOperand(MCOperand::createReg(FRHi));
    EmitToStreamer(*OutStreamer, M2);
    EmitAdd(AddrReg, -4);
  } else if (Opc == SH::FMOV_D_R0IND) {
    MCInst M1;
    M1.setOpcode(SH::FMOV_S_R0IND);
    M1.addOperand(MCOperand::createReg(FRLo));
    M1.addOperand(MCOperand::createReg(AddrReg));
    EmitToStreamer(*OutStreamer, M1);
    EmitAdd(SH::R0, 4);
    MCInst M2;
    M2.setOpcode(SH::FMOV_S_R0IND);
    M2.addOperand(MCOperand::createReg(FRHi));
    M2.addOperand(MCOperand::createReg(AddrReg));
    EmitToStreamer(*OutStreamer, M2);
    EmitAdd(SH::R0, -4);
  } else if (Opc == SH::FMOV_D_R0STO) {
    MCInst M1;
    M1.setOpcode(SH::FMOV_S_R0STO);
    M1.addOperand(MCOperand::createReg(AddrReg));
    M1.addOperand(MCOperand::createReg(FRLo));
    EmitToStreamer(*OutStreamer, M1);
    EmitAdd(SH::R0, 4);
    MCInst M2;
    M2.setOpcode(SH::FMOV_S_R0STO);
    M2.addOperand(MCOperand::createReg(AddrReg));
    M2.addOperand(MCOperand::createReg(FRHi));
    EmitToStreamer(*OutStreamer, M2);
    EmitAdd(SH::R0, -4);
  }
}

void SHAsmPrinter::emitBranchFar(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();

  // COND_BR_FAR_T/F: emit inverted short branch to skip the long jump.
  if (Opc == SH::COND_BR_FAR_T || Opc == SH::COND_BR_FAR_F) {
    unsigned SkipOpc = (Opc == SH::COND_BR_FAR_T) ? SH::BF : SH::BT;
    MCInst Skip;
    Skip.setOpcode(SkipOpc);
    Skip.addOperand(MCOperand::createImm(3)); // disp=3 → skip 8 bytes
    EmitToStreamer(*OutStreamer, Skip);
  }

  // BR_FAR / COND_BR_FAR: push R0; mov.l @CPI, R0; jmp @R0; pop R0
  MCSymbol *CPISym = GetCPISymbol(MI.getOperand(1).getIndex());
  const MCExpr *CPIExpr = MCSymbolRefExpr::create(CPISym, OutContext);

  MCInst Push;
  Push.setOpcode(SH::MOVL_DEC);
  Push.addOperand(MCOperand::createReg(SH::R15));
  Push.addOperand(MCOperand::createReg(SH::R0));
  Push.addOperand(MCOperand::createReg(SH::R15));
  EmitToStreamer(*OutStreamer, Push);

  MCInst Mov;
  Mov.setOpcode(SH::MOVL_PCREL);
  Mov.addOperand(MCOperand::createReg(SH::R0));
  Mov.addOperand(MCOperand::createExpr(CPIExpr));
  EmitToStreamer(*OutStreamer, Mov);

  MCInst Jmp;
  Jmp.setOpcode(SH::JMP);
  Jmp.addOperand(MCOperand::createReg(SH::R0));
  EmitToStreamer(*OutStreamer, Jmp);

  MCInst Pop;
  Pop.setOpcode(SH::MOVL_INC);
  Pop.addOperand(MCOperand::createReg(SH::R0));
  Pop.addOperand(MCOperand::createReg(SH::R15));
  Pop.addOperand(MCOperand::createReg(SH::R15));
  EmitToStreamer(*OutStreamer, Pop);
}

void SHAsmPrinter::emitInstruction(const MachineInstr *MI) {
  MachineBasicBlock::const_instr_iterator I = MI->getIterator();
  MachineBasicBlock::const_instr_iterator E = MI->getParent()->instr_end();

  do {
    if (I->isBundle())
      continue;

    // MOV_I32: all operands are CPI (routed through MachineConstantPool
    // by ISelLowering::wrapAddress, GISel, and CI pass JT placement).
    if (I->getOpcode() == SH::MOV_I32) {
      const MachineOperand &MO = I->getOperand(1);
      assert(MO.isCPI() && "MOV_I32 operand must be CPI");
      MCInst Mov;
      Mov.setOpcode(SH::MOVL_PCREL);
      Mov.addOperand(MCOperand::createReg(I->getOperand(0).getReg()));
      MCSymbol *ValSym = GetCPISymbol(MO.getIndex());
      const MCExpr *Expr = MCSymbolRefExpr::create(ValSym, OutContext);
      Mov.addOperand(MCOperand::createExpr(Expr));
      EmitToStreamer(*OutStreamer, Mov);
      continue;
    }

    if (I->getOpcode() == SH::FMOV_D_LD || I->getOpcode() == SH::FMOV_D_ST ||
        I->getOpcode() == SH::FMOV_D_INC || I->getOpcode() == SH::FMOV_D_DEC ||
        I->getOpcode() == SH::FMOV_D_R0IND ||
        I->getOpcode() == SH::FMOV_D_R0STO) {
      emitFMOVDouble(*I);
      continue;
    }

    if (I->getOpcode() == SH::BR_FAR || I->getOpcode() == SH::COND_BR_FAR_T ||
        I->getOpcode() == SH::COND_BR_FAR_F) {
      emitBranchFar(*I);
      continue;
    }

    if (I->getOpcode() == SH::MOV_GOT) {
      // MOV_GOT CPI → mova @CPI, R0 + mov.l @CPI, Rn + add R0, Rn
      // The CPI entry contains _GLOBAL_OFFSET_TABLE_@GOTPC (placed by CI pass).
      unsigned DstReg = I->getOperand(0).getReg();
      MCSymbol *CPISym = GetCPISymbol(I->getOperand(1).getIndex());
      const MCExpr *CPIExpr = MCSymbolRefExpr::create(CPISym, OutContext);

      MCInst Mova;
      Mova.setOpcode(SH::MOVA);
      Mova.addOperand(MCOperand::createExpr(CPIExpr));
      EmitToStreamer(*OutStreamer, Mova);

      MCInst MovL;
      MovL.setOpcode(SH::MOVL_PCREL);
      MovL.addOperand(MCOperand::createReg(DstReg));
      MovL.addOperand(MCOperand::createExpr(CPIExpr));
      EmitToStreamer(*OutStreamer, MovL);

      MCInst Add;
      Add.setOpcode(SH::ADD_RR);
      Add.addOperand(MCOperand::createReg(DstReg));
      Add.addOperand(MCOperand::createReg(DstReg));
      Add.addOperand(MCOperand::createReg(SH::R0));
      EmitToStreamer(*OutStreamer, Add);

      continue;
    }

    if (I->getOpcode() == SH::MUL32_PSEUDO) {
      MCInst Mul;
      Mul.setOpcode(SH::MUL_L);
      Mul.addOperand(MCOperand::createReg(I->getOperand(1).getReg()));
      Mul.addOperand(MCOperand::createReg(I->getOperand(2).getReg()));
      EmitToStreamer(*OutStreamer, Mul);

      MCInst Sts;
      Sts.setOpcode(SH::STS_MACL);
      Sts.addOperand(MCOperand::createReg(I->getOperand(0).getReg()));
      EmitToStreamer(*OutStreamer, Sts);

      continue;
    }

    if (I->getOpcode() == SH::MULHS_PSEUDO ||
        I->getOpcode() == SH::MULHU_PSEUDO) {
      bool isUnsigned = (I->getOpcode() == SH::MULHU_PSEUDO);
      MCInst Mul;
      Mul.setOpcode(isUnsigned ? SH::DMULU_L : SH::DMULS_L);
      Mul.addOperand(MCOperand::createReg(I->getOperand(1).getReg()));
      Mul.addOperand(MCOperand::createReg(I->getOperand(2).getReg()));
      EmitToStreamer(*OutStreamer, Mul);

      MCInst Sts;
      Sts.setOpcode(SH::STS_MACH);
      Sts.addOperand(MCOperand::createReg(I->getOperand(0).getReg()));
      EmitToStreamer(*OutStreamer, Sts);

      continue;
    }

    if (I->getOpcode() == SH::FP_TO_SINT_F32 ||
        I->getOpcode() == SH::FP_TO_SINT_F64) {
      bool isDouble = (I->getOpcode() == SH::FP_TO_SINT_F64);
      MCInst Ftrc;
      Ftrc.setOpcode(isDouble ? SH::FTRC_D : SH::FTRC_S);
      Ftrc.addOperand(MCOperand::createReg(I->getOperand(1).getReg()));
      EmitToStreamer(*OutStreamer, Ftrc);

      MCInst Sts;
      Sts.setOpcode(SH::STS_FPUL);
      Sts.addOperand(MCOperand::createReg(I->getOperand(0).getReg()));
      EmitToStreamer(*OutStreamer, Sts);

      continue;
    }

    if (I->getOpcode() == SH::SINT_TO_FP_F32 ||
        I->getOpcode() == SH::SINT_TO_FP_F64) {
      bool isDouble = (I->getOpcode() == SH::SINT_TO_FP_F64);

      MCInst Lds;
      Lds.setOpcode(SH::LDS_FPUL);
      Lds.addOperand(MCOperand::createReg(I->getOperand(1).getReg()));
      EmitToStreamer(*OutStreamer, Lds);

      MCInst FloatOp;
      FloatOp.setOpcode(isDouble ? SH::FLOAT_D : SH::FLOAT_S);
      FloatOp.addOperand(MCOperand::createReg(I->getOperand(0).getReg()));
      EmitToStreamer(*OutStreamer, FloatOp);

      continue;
    }

    if (I->getOpcode() == SH::FP_EXTEND_F32_F64) {
      MCInst Flds;
      Flds.setOpcode(SH::FLDS);
      Flds.addOperand(MCOperand::createReg(I->getOperand(1).getReg()));
      EmitToStreamer(*OutStreamer, Flds);

      MCInst Fcnvsd;
      Fcnvsd.setOpcode(SH::FCNVSD);
      Fcnvsd.addOperand(MCOperand::createReg(I->getOperand(0).getReg()));
      EmitToStreamer(*OutStreamer, Fcnvsd);

      continue;
    }

    if (I->getOpcode() == SH::FP_ROUND_F64_F32) {
      MCInst Fcnvds;
      Fcnvds.setOpcode(SH::FCNVDS);
      Fcnvds.addOperand(MCOperand::createReg(I->getOperand(1).getReg()));
      EmitToStreamer(*OutStreamer, Fcnvds);

      MCInst Fsts;
      Fsts.setOpcode(SH::FSTS);
      Fsts.addOperand(MCOperand::createReg(I->getOperand(0).getReg()));
      EmitToStreamer(*OutStreamer, Fsts);

      continue;
    }

    if (I->getOpcode() == SH::BITCAST_F32_TO_I32) {
      MCInst Flds;
      Flds.setOpcode(SH::FLDS);
      Flds.addOperand(MCOperand::createReg(I->getOperand(1).getReg()));
      EmitToStreamer(*OutStreamer, Flds);

      MCInst Sts;
      Sts.setOpcode(SH::STS_FPUL);
      Sts.addOperand(MCOperand::createReg(I->getOperand(0).getReg()));
      EmitToStreamer(*OutStreamer, Sts);

      continue;
    }

    if (I->getOpcode() == SH::BITCAST_I32_TO_F32) {
      MCInst Lds;
      Lds.setOpcode(SH::LDS_FPUL);
      Lds.addOperand(MCOperand::createReg(I->getOperand(1).getReg()));
      EmitToStreamer(*OutStreamer, Lds);

      MCInst Fsts;
      Fsts.setOpcode(SH::FSTS);
      Fsts.addOperand(MCOperand::createReg(I->getOperand(0).getReg()));
      EmitToStreamer(*OutStreamer, Fsts);

      continue;
    }

    if (I->getOpcode() == SH::CONSTPOOL_ENTRY) {
      unsigned LabelId = (unsigned)I->getOperand(0).getImm();
      unsigned CPIdx = (unsigned)I->getOperand(1).getIndex();

      OutStreamer->emitLabel(GetCPISymbol(LabelId));

      const MachineConstantPoolEntry &MCPE =
          MI->getMF()->getConstantPool()->getConstants()[CPIdx];
      if (MCPE.isMachineConstantPoolEntry()) {
        emitMachineConstantPoolValue(MCPE.Val.MachineCPVal);
      } else {
        // Use emitGlobalConstant instead of directly emitting bytes
        // because constants may be expressions needing relocations.
        emitGlobalConstant(MI->getMF()->getDataLayout(), MCPE.Val.ConstVal);
      }
      continue;
    }

    SHMCInstLower MCInstLowering(OutContext, *this);

    // BT/BF far-branch expansion is handled by SHBranchExpansion pass
    // which runs before the CI pass.  No AsmPrinter expansion needed.

    MCInst TmpInst;
    MCInstLowering.Lower(&*I, TmpInst);
    EmitToStreamer(*OutStreamer, TmpInst);
  } while ((++I != E) && I->isInsideBundle());
}

bool SHAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                   const char *ExtraCode, raw_ostream &OS) {
  // Default operand printing.
  if (!ExtraCode || !ExtraCode[0]) {
    const MachineOperand &MO = MI->getOperand(OpNo);
    switch (MO.getType()) {
    case MachineOperand::MO_Register:
      OS << SHInstPrinter::getRegisterName(MO.getReg());
      return false;
    case MachineOperand::MO_Immediate:
      OS << MO.getImm();
      return false;
    default:
      break;
    }
  }
  return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS);
}

bool SHAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                         const char *ExtraCode,
                                         raw_ostream &OS) {
  if (!ExtraCode || !ExtraCode[0]) {
    const MachineOperand &MO = MI->getOperand(OpNo);
    OS << "@" << SHInstPrinter::getRegisterName(MO.getReg());
    return false;
  }
  return AsmPrinter::PrintAsmMemoryOperand(MI, OpNo, ExtraCode, OS);
}

void SHAsmPrinter::emitMachineConstantPoolValue(
    MachineConstantPoolValue *MCPV) {
  auto *SCPV = static_cast<SHConstantPoolValue *>(MCPV);
  MCSymbol *MCSym = nullptr;
  SHCP::SHCPModifier Modifier = SHCP::no_modifier;

  if (auto *GVV = dyn_cast<SHConstantPoolGV>(SCPV)) {
    MCSym = getSymbol(GVV->getGlobalValue());
    Modifier = GVV->getModifier();
  } else if (auto *SV = dyn_cast<SHConstantPoolSymbol>(SCPV)) {
    MCSym = GetExternalSymbolSymbol(SV->getSymbol());
    Modifier = SV->getModifier();
  } else if (auto *BV = dyn_cast<SHConstantPoolBA>(SCPV)) {
    MCSym = GetBlockAddressSymbol(BV->getBlockAddress());
    const MCExpr *Expr = MCSymbolRefExpr::create(MCSym, OutContext);
    if (BV->getOffset())
      Expr = MCBinaryExpr::createAdd(
          Expr, MCConstantExpr::create(BV->getOffset(), OutContext),
          OutContext);
    OutStreamer->emitValue(Expr, 4);
    return;
  } else if (auto *MV = dyn_cast<SHConstantPoolMBB>(SCPV)) {
    MCSym = MV->getMBB()->getSymbol();
  } else if (auto *JV = dyn_cast<SHConstantPoolJTI>(SCPV)) {
    MCSym = GetJTISymbol(JV->getJumpTableIndex());
  } else if (auto *CV = dyn_cast<SHConstantPoolCPIAddr>(SCPV)) {
    MCSym = GetCPISymbol(CV->getCPIndex());
  } else {
    llvm_unreachable("Unknown SHConstantPoolValue kind");
  }

  // Apply relocation modifier (GOT, GOTPC, TPOFF) if present.
  const MCExpr *Expr;
  switch (Modifier) {
  case SHCP::GOT:
    Expr = MCSymbolRefExpr::create(MCSym, SH::S_GOT, OutContext);
    break;
  case SHCP::GOTPC:
    Expr = MCSymbolRefExpr::create(MCSym, SH::S_GOTPC, OutContext);
    break;
  case SHCP::TPOFF:
    Expr = MCSymbolRefExpr::create(MCSym, SH::S_TPOFF, OutContext);
    break;
  default:
    Expr = MCSymbolRefExpr::create(MCSym, OutContext);
    break;
  }
  OutStreamer->emitValue(Expr, 4);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSHAsmPrinter() {
  RegisterAsmPrinter<SHAsmPrinter> X(getTheSHTarget());
}
