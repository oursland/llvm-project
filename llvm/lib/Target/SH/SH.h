//===- SH.h - Top-level interface for SH representation -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SH_SH_H
#define LLVM_LIB_TARGET_SH_SH_H

#include "MCTargetDesc/SHMCTargetDesc.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class SHTargetMachine;
class FunctionPass;
class PassRegistry;

FunctionPass *createSHISelDag(SHTargetMachine &TM, CodeGenOptLevel OptLevel);
FunctionPass *createSHDelaySlotFillerPass();
FunctionPass *createSHBranchExpansionPass();
FunctionPass *createSHConstantIslandPass();
FunctionPass *createSHFPSCRPass();
FunctionPass *createSHDTCombinePass();

void initializeSHConstantIslandsPass(PassRegistry &);

void initializeSHBranchExpansionPass(PassRegistry &);
void initializeSHAsmPrinterPass(PassRegistry &);
void initializeSHDAGToDAGISelLegacyPass(PassRegistry &);
void initializeSHDelaySlotFillerPass(PassRegistry &);
void initializeSHDTCombinePass(PassRegistry &);
void initializeSHFPSCRPassPass(PassRegistry &);

/// SH condition codes — passed as integer operands in BRCC pseudo.
namespace SHCC {
enum CondCode : unsigned {
  EQ = 0,  // equal
  NE = 1,  // not equal
  LT = 2,  // signed less-than
  LE = 3,  // signed less-or-equal
  GT = 4,  // signed greater-than
  GE = 5,  // signed greater-or-equal
  ULT = 6, // unsigned less-than
  ULE = 7, // unsigned less-or-equal
  UGT = 8, // unsigned greater-than
  UGE = 9, // unsigned greater-or-equal
};

/// Bit set in the encoded CC immediate to indicate the branch sense is
/// inverted (BF instead of BT).
static constexpr unsigned InvertBit = 0x80;

/// Mask for extracting the condition code from an encoded CC.
static constexpr unsigned CCMask = 0x7F;
} // namespace SHCC

/// Return the maximum forward branch displacement (in bytes) for the
/// given branch opcode, or 0 if unknown.
inline int64_t getMaxBranchDisp(unsigned Opc) {
  switch (Opc) {
  case SH::BT:
  case SH::BF:
  case SH::BT_S:
  case SH::BF_S:
    return 254; // Signed 8-bit * 2 = [-256, +254]
  case SH::BRA:
    return 4094; // Signed 12-bit * 2 = [-4096, +4094]
  default:
    return 0;
  }
}

/// SH ISD node types.
namespace SHISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  RET,              // Return (expanded from ISD::RETURN)
  CALL,             // Direct call  (BSR / BSR+NOP)
  CALLR,            // Indirect call (JSR @Rn + NOP)
  WRAPPER,          // PC-relative address wrapper
  BRCC,             // Compare + conditional branch
  GOTAddr,          // Load GOT base address into register
  GlobalAddressPIC, // Wraps PIC target variables
  READ_GBR,         // Read Global Base Register for TLS
  WrapperTLS,       // TLS offset wrapper
  EH_RETURN,        // Exception handling return
  SHLD,             // Shift Logical Dynamic
  SHAD,             // Shift Arithmetic Dynamic
  CMP,              // Compare (sets T bit)
  MOVT,             // Move T bit to register
  CPI_ADDR,         // Compute address of constant pool entry (for FP constants)
};
} // namespace SHISD

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SH_SH_H
