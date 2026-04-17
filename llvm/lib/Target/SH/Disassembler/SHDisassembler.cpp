//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH disassembler.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SHMCTargetDesc.h"
#include "TargetInfo/SHTargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;

#define DEBUG_TYPE "sh-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {
class SHDisassembler : public MCDisassembler {
public:
  SHDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};
} // end anonymous namespace

static MCDisassembler *createSHDisassembler(const Target &T,
                                            const MCSubtargetInfo &STI,
                                            MCContext &Ctx) {
  return new SHDisassembler(STI, Ctx);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSHDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheSHTarget(),
                                         createSHDisassembler);
}

static const uint16_t GPRDecoderTable[] = {
    SH::R0, SH::R1, SH::R2,  SH::R3,  SH::R4,  SH::R5,  SH::R6,  SH::R7,
    SH::R8, SH::R9, SH::R10, SH::R11, SH::R12, SH::R13, SH::R14, SH::R15};

static DecodeStatus DecodeGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  if (RegNo > 15)
    return MCDisassembler::Fail;

  unsigned Reg = GPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPR32RegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo > 15)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(SH::FR0 + RegNo));
  return MCDisassembler::Success;
}

static const uint16_t FPR64DecoderTable[] = {
    SH::DR0, 0, SH::DR2,  0, SH::DR4,  0, SH::DR6,  0,
    SH::DR8, 0, SH::DR10, 0, SH::DR12, 0, SH::DR14, 0};

static DecodeStatus DecodeFPR64RegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  // DR registers use even-numbered FRn encodings: 0, 2, 4, ...
  if (RegNo > 15 || (RegNo & 1) != 0)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(FPR64DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPV32RegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo > 3)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(SH::FV0 + (RegNo * 4)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeBranchTarget8(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  // SH BT/BF/BT.S/BF.S: target = PC + 4 + SignExtend(disp8) * 2
  int32_t Offset = SignExtend32<8>(Insn);
  uint64_t Target = Address + 4 + (Offset * 2);
  Inst.addOperand(MCOperand::createImm(Target));
  return MCDisassembler::Success;
}

static DecodeStatus decodeBranchTarget12(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  // SH BRA/BSR: target = PC + 4 + SignExtend(disp12) * 2
  int32_t Offset = SignExtend32<12>(Insn);
  uint64_t Target = Address + 4 + (Offset * 2);
  Inst.addOperand(MCOperand::createImm(Target));
  return MCDisassembler::Success;
}

static uint64_t fieldFromInstruction(uint64_t insn, unsigned startBit,
                                     unsigned numBits) {
  assert(startBit + numBits <= 64 && "Can't extract more than 64 bits!");
  if (numBits == 64)
    return insn;
  return (insn >> startBit) & ((1ULL << numBits) - 1);
}

#define Check(S, Decode) (((S) = Decode) != MCDisassembler::Fail)

using namespace llvm::MCD;
#include "SHGenDisassemblerTables.inc"

DecodeStatus SHDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                            ArrayRef<uint8_t> Bytes,
                                            uint64_t Address,
                                            raw_ostream &CStream) const {
  if (Bytes.size() < 2) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  uint32_t Insn = support::endian::read16le(Bytes.data());

  DecodeStatus Result =
      decodeInstruction(DecoderTableSH16, Instr, Insn, Address, this, STI);

  if (Result != MCDisassembler::Fail) {
    Size = 2;
    return Result;
  }
  // SH instructions are always 2 bytes. Even on decode failure, advance
  // by 2 bytes to maintain instruction stream alignment. Setting Size=0
  // causes the disassembly engine to advance by 1 byte, permanently
  // misaligning all subsequent instruction decoding.
  Size = 2;
  return MCDisassembler::Fail;
}
