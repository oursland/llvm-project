//===- SHInstrFormats.td - SH Instruction Formats --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// All SH instructions are 16 bits wide (fixed-length encoding).
// Bit numbering: bit 15 = MSB, bit 0 = LSB (as per the SH7750 manual).
//
// Common formats:
//   0000 nnnn mmmm xxxx   (F_RR with op=0b0000)
//   0001 nnnn mmmm dddd   (F_RRD4)
//   0010 nnnn mmmm xxxx   (F_RR with op=0b0010)
//   0011 nnnn mmmm xxxx   (F_RR with op=0b0011)
//   0100 nnnn xxxx xxxx   (F_R)
//   0101 nnnn mmmm dddd   (F_RRD4)
//   0110 nnnn mmmm xxxx   (F_RR with op=0b0110)
//   0111 nnnn iiii iiii   (F_RI8)
//   1000 xxxx mmmm dddd   (F_R_D4)
//   1001 nnnn dddd dddd   (F_RD8)
//   1010 dddd dddd dddd   (F_D12)
//   1011 dddd dddd dddd   (F_D12)
//   1100 xxxx iiii iiii   (F_I8 / F_D8)
//   1101 nnnn dddd dddd   (F_RD8)
//   1110 nnnn iiii iiii   (F_RI8)
//   1111 nnnn mmmm xxxx   (F_FRR / F_FLD / F_FST)
//   1111 nnnn xxxx xxxx   (F_FR)
//
//===----------------------------------------------------------------------===//

// Base instruction class
class SHInst<dag outs, dag ins, string asmstr, list<dag> pattern,
              InstrItinClass itin = NoItinerary>
   : Instruction {
  field bits<16> Inst;
  bits<16> SoftFail = 0;

  let Namespace = "SH";
  let DecoderNamespace = "SH";
  let Size = 2;

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString   = asmstr;
  let Pattern = pattern;
  let Itinerary = itin;
}

// SH Pseudo Instructions format
class SHPseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

//===----------------------------------------------------------------------===//
// Two-register formats
//===----------------------------------------------------------------------===//

// Format: opcode[15:12] | Rn[11:8] | Rm[7:4] | subop[3:0]
class F_RR<bits<4> op, bits<4> subop, dag outs, dag ins, string asmstr,
           list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<4> Rn;
  bits<4> Rm;
  let Inst{15-12} = op;
  let Inst{11-8}  = Rn;
  let Inst{7-4}   = Rm;
  let Inst{3-0}   = subop;
}

// Format: opcode[15:12] | Rn[11:8] | Rm[7:4] | disp4[3:0]
class F_RRD4<bits<4> op, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<4> Rn;
  bits<4> Rm;
  bits<4> disp4;
  let Inst{15-12} = op;
  let Inst{11-8}  = Rn;
  let Inst{7-4}   = Rm;
  let Inst{3-0}   = disp4;
}

//===----------------------------------------------------------------------===//
// Single-register formats
//===----------------------------------------------------------------------===//

// Format: opcode[15:12] | Rn[11:8] | subop[7:0]
// The register Rn is always at bits[11:8] in SH single-register instructions.
class F_R<bits<4> op, bits<8> subop, dag outs, dag ins, string asmstr,
          list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<4> Rn;
  let Inst{15-12} = op;
  let Inst{11-8}  = Rn;
  let Inst{7-0}   = subop;
}

// Format: opcode[15:8] | Rm[7:4] | disp4[3:0]
// Used by: MOV.B/W @(disp,Rm),R0 and MOV.B/W R0,@(disp,Rn)
// R0 is implicit (either source or destination).
class F_R_D4<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<4> Rm;    // base register (bits[7:4])
  bits<4> disp4; // displacement  (bits[3:0])
  let Inst{15-8} = op;
  let Inst{7-4}  = Rm;
  let Inst{3-0}  = disp4;
}

//===----------------------------------------------------------------------===//
// Immediate and displacement formats
//===----------------------------------------------------------------------===//

// Format: opcode[15:12] | Rn[11:8] | imm8[7:0]
class F_RI8<bits<4> op, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<4> Rn;
  bits<8> imm8;
  let Inst{15-12} = op;
  let Inst{11-8}  = Rn;
  let Inst{7-0}   = imm8;
}

// Format: opcode[15:12] | Rn[11:8] | disp8[7:0]
// Used by: MOV.L/W @(disp*N,PC),Rn
class F_RD8<bits<4> op, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<4> Rn;
  bits<8> disp8;
  let Inst{15-12} = op;
  let Inst{11-8}  = Rn;
  let Inst{7-0}   = disp8;
}

// Format: opcode[15:8] | imm8[7:0]
// Used by: AND/OR/XOR/TST #imm8,R0  and  TRAPA #imm8  and  GBR ops
class F_I8<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<8> imm8;
  let Inst{15-8} = op;
  let Inst{7-0}  = imm8;
}

// Format: opcode[15:8] | disp8[7:0]
// Used by: BT, BF, BT/S, BF/S, MOVA, GBR load/store with 8-bit displacement
class F_D8<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<8> disp8;
  let Inst{15-8} = op;
  let Inst{7-0}  = disp8;
}

// Format: opcode[15:12] | disp12[11:0]
// Used by: BRA, BSR
class F_D12<bits<4> op, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<12> disp12;
  let Inst{15-12} = op;
  let Inst{11-0}  = disp12;
}

// Format: fixed 16-bit opcode (no register operands)
class F_Bare<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  let Inst = op;
}

//===----------------------------------------------------------------------===//
// Floating-point formats  (major opcode group = 1111)
//===----------------------------------------------------------------------===//

// FP two-register: 1111 | FRn[11:8] | FRm[7:4] | subop[3:0]
class F_FRR<bits<4> subop, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<4> FRn;
  bits<4> FRm;
  let Inst{15-12} = 0b1111;
  let Inst{11-8}  = FRn;
  let Inst{7-4}   = FRm;
  let Inst{3-0}   = subop;
}

// FP single-register: opcode[15:12] | FRn[11:8] | subop[7:0]
// The op nibble is almost always 0b1111 for FP instructions.
class F_FR<bits<4> op, bits<8> subop, dag outs, dag ins, string asmstr,
           list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<4> FRn;
  let Inst{15-12} = op;
  let Inst{11-8}  = FRn;
  let Inst{7-0}   = subop;
}

// FP load (memory → FP register): 1111 | FRn[11:8] | Rm(GPR)[7:4] | subop[3:0]
class F_FLD<bits<4> subop, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<4> FRn;   // FP destination register
  bits<4> Rm;    // GPR base / index register
  let Inst{15-12} = 0b1111;
  let Inst{11-8}  = FRn;
  let Inst{7-4}   = Rm;
  let Inst{3-0}   = subop;
}

// FP store (FP register → memory): 1111 | Rn(GPR)[11:8] | FRm[7:4] | subop[3:0]
class F_FST<bits<4> subop, dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<4> Rn;    // GPR base / index register
  bits<4> FRm;   // FP source register
  let Inst{15-12} = 0b1111;
  let Inst{11-8}  = Rn;
  let Inst{7-4}   = FRm;
  let Inst{3-0}   = subop;
}

// FIPR FVm,FVn — inner product of 4-element float vectors
// 1111 | FVn[11:10] | FVm[9:8] | 1110[7:4] | 1101[3:0]
// FV register encoding: FV0=0b00, FV4=0b01, FV8=0b10, FV12=0b11
class F_FIPR<dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<2> FVn;
  bits<2> FVm;
  let Inst{15-12} = 0b1111;
  let Inst{11-10} = FVn;
  let Inst{9-8}   = FVm;
  let Inst{7-4}   = 0b1110;
  let Inst{3-0}   = 0b1101;
}

// FTRV XMTRX,FVn — 4×4 matrix × 4-vector multiply
// 1111 | FVn[11:10] | 01[9:8] | 1111[7:4] | 1101[3:0]
class F_FTRV<dag outs, dag ins, string asmstr, list<dag> pattern>
    : SHInst<outs, ins, asmstr, pattern> {
  bits<2> FVn;
  let Inst{15-12} = 0b1111;
  let Inst{11-10} = FVn;
  let Inst{9-8}   = 0b01;
  let Inst{7-4}   = 0b1111;
  let Inst{3-0}   = 0b1101;
}

//===----------------------------------------------------------------------===//
// Operand types
//===----------------------------------------------------------------------===//

def simm8  : Operand<i32>, ImmLeaf<i32, [{ return isInt<8>(Imm); }]>;
def uimm8  : Operand<i32>, ImmLeaf<i32, [{ return isUInt<8>(Imm); }]>;
def uimm4  : Operand<i32>, ImmLeaf<i32, [{ return isUInt<4>(Imm); }]>;

// PC-relative displacement operand — accepts symbols and expressions,
// not just small constants.  The actual 8-bit displacement is computed
// by the linker/encoder, not validated at parse time.
def disp8_pcrel : Operand<i32>;

// Branch target operands
def brtarget8  : Operand<OtherVT> {
  let PrintMethod  = "printBrTarget";
  let EncoderMethod = "encodeBranchTarget8";
  let DecoderMethod = "decodeBranchTarget8";
}
def brtarget12 : Operand<OtherVT> {
  let PrintMethod  = "printBrTarget";
  let EncoderMethod = "encodeBranchTarget12";
  let DecoderMethod = "decodeBranchTarget12";
}

// Call target operand
def calltarget : Operand<i32> {
  let PrintMethod  = "printBrTarget";
  let EncoderMethod = "encodeBranchTarget12";
  let DecoderMethod = "decodeBranchTarget12";
}

// Memory operands (used in inline-asm constraints)
def MEMrr : Operand<i32> {
  let MIOperandInfo = (ops GPR:$base, GPR:$index);
  let PrintMethod   = "printMemRR";
}
def MEMri : Operand<i32> {
  let MIOperandInfo = (ops GPR:$base, simm8:$disp);
  let PrintMethod   = "printMemRI";
}
