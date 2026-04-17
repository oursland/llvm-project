//===- SHInstrInfo.td - SH Instruction Descriptions ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "SHInstrFormats.td"

//===----------------------------------------------------------------------===//
// SH-specific SDNode types
//===----------------------------------------------------------------------===//

def HasSH4A : Predicate<"Subtarget->isSH4A()">, AssemblerPredicate<(all_of FeatureSH4A)>;
def HasFPU  : Predicate<"Subtarget->hasFPU()">, AssemblerPredicate<(all_of FeatureFPU)>;
def HasDPFPU: Predicate<"Subtarget->hasDPFPU()">, AssemblerPredicate<(all_of FeatureDPFPU)>;

def SDT_SHCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_SHCallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_SHCall    : SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>;
def SDT_SHRet     : SDTypeProfile<0, 0, []>;
def SDT_SHWrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;
def SDT_SHCmpBr   : SDTypeProfile<0, 4, [SDTCisSameAs<0,1>, SDTCisVT<2, i32>]>;

def SHretflag : SDNode<"SHISD::RET",    SDT_SHRet,
                         [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def SHcall    : SDNode<"SHISD::CALL",   SDT_SHCall,
                         [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                          SDNPVariadic]>;
def SHcallr   : SDNode<"SHISD::CALLR",  SDT_SHCall,
                         [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                          SDNPVariadic]>;
def SHwrapper : SDNode<"SHISD::WRAPPER", SDT_SHWrapper, []>;
def SHbrcc    : SDNode<"SHISD::BRCC",   SDT_SHCmpBr, [SDNPHasChain]>;
def SHGOTAddr : SDNode<"SHISD::GOTAddr", SDT_SHWrapper, []>;
def SHread_gbr : SDNode<"SHISD::READ_GBR", SDTypeProfile<1, 0, [SDTCisVT<0, i32>]>, []>;
def SHcpi_addr : SDNode<"SHISD::CPI_ADDR", SDT_SHWrapper, []>;
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_SHCallSeqStart,
                            [SDNPHasChain, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END",   SDT_SHCallSeqEnd,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def SHeh_return  : SDNode<"SHISD::EH_RETURN",
                           SDTypeProfile<0, 2, [SDTCisPtrTy<0>, SDTCisPtrTy<1>]>,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

//===----------------------------------------------------------------------===//
// Pseudo Instructions
//===----------------------------------------------------------------------===//

defm : RemapAllTargetPseudoPointerOperands<GPR>;

let isReMaterializable = 1, isMoveImm = 1, isAsCheapAsAMove = 1 in
def MOV_I32 : SHPseudo<(outs GPR:$dst), (ins i32imm:$imm),
                        "# MOV_I32\t$imm, $dst",
                        []>;

// CPI_ADDR: compute the ADDRESS of a constant pool entry.
// Unlike MOV_I32 @CPI which loads the CPI data value, CPI_ADDR
// puts the CPI label address into the literal pool and loads it.
// Used for FP constant materialization where we need the address
// of the CPI data, not the data itself.
let isReMaterializable = 1, isMoveImm = 1 in
def CPI_ADDR : SHPseudo<(outs GPR:$dst), (ins i32imm:$cpi),
                         "# CPI_ADDR\t$cpi, $dst",
                         []>;

def : Pat<(SHwrapper tglobaladdr:$imm), (MOV_I32 tglobaladdr:$imm)>;
def : Pat<(SHwrapper texternalsym:$imm), (MOV_I32 texternalsym:$imm)>;
def : Pat<(SHwrapper tconstpool:$imm), (MOV_I32 tconstpool:$imm)>;
def : Pat<(SHwrapper tblockaddress:$imm), (MOV_I32 tblockaddress:$imm)>;
def : Pat<(SHwrapper tjumptable:$imm), (MOV_I32 tjumptable:$imm)>;
def : Pat<(SHcpi_addr tconstpool:$cpi), (CPI_ADDR tconstpool:$cpi)>;

let isReMaterializable = 1, isMoveImm = 1 in {
  // MOV_GOT: load GOT base address from a CPI entry containing
  // _GLOBAL_OFFSET_TABLE_@GOTPC. Expands to: mova @CPI + mov.l @CPI + add R0.
  def MOV_GOT : SHPseudo<(outs GPR:$dst), (ins i32imm:$cpi),
                          "# MOV_GOT\t$cpi, $dst",
                          []> { let Defs = [R0]; }
}
def : Pat<(SHwrapper tjumptable:$imm), (MOV_I32 tjumptable:$imm)>;


let Defs = [R15], Uses = [R15] in {
  def ADJCALLSTACKDOWN : SHPseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                                   "# ADJCALLSTACKDOWN $amt1, $amt2",
                                   [(callseq_start timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP   : SHPseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                                   "# ADJCALLSTACKUP $amt1, $amt2",
                                   [(callseq_end timm:$amt1, timm:$amt2)]>;
}

// EH_RETURN: Pseudo instruction for exception handling return
let isTerminator = 1, isReturn = 1, isBarrier = 1, hasCtrlDep = 1,
    Uses = [R12, R13] in
def EH_RETURN : SHPseudo<(outs), (ins),
                          "! EH_RETURN\n\tmov r13, r15\n\tjmp @r12\n\tnop",
                          [(SHeh_return R12, R13)]>;

def GETFI : SHPseudo<(outs GPR:$dst), (ins GPR:$base, simm8:$offset),
                       "# GETFI $dst, $base, $offset", []>;

//===----------------------------------------------------------------------===//
// MOV — Register, Immediate, PC-Relative
//===----------------------------------------------------------------------===//

// MOV Rm,Rn  (0110 nnnn mmmm 0011)
def MOV_RR : F_RR<0b0110, 0b0011,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "mov\t$Rm, $Rn", []> { let isMoveImm = 1; }

// MOV #imm8,Rn  (1110 nnnn iiii iiii)
def MOV_I8 : F_RI8<0b1110,
  (outs GPR:$Rn), (ins simm8:$imm8),
  "mov\t#$imm8, $Rn",
  [(set GPR:$Rn, simm8:$imm8)]>;

// MOVT Rn  — move T bit to Rn  (0000 nnnn 0010 1001)
def MOVT : F_R<0b0000, 0b00101001,
  (outs GPR:$Rn), (ins),
  "movt\t$Rn", []> { let Uses = [SR]; }

// MOV.L @(disp8*4,PC),Rn  (1101 nnnn dddd dddd)
let isReMaterializable = 1, mayLoad = 1 in
def MOVL_PCREL : F_RD8<0b1101,
  (outs GPR:$Rn), (ins disp8_pcrel:$disp8),
  "mov.l\t@($disp8, pc), $Rn", []>;

// MOV.W @(disp8*2,PC),Rn  (1001 nnnn dddd dddd)
let isReMaterializable = 1, mayLoad = 1 in
def MOVW_PCREL : F_RD8<0b1001,
  (outs GPR:$Rn), (ins disp8_pcrel:$disp8),
  "mov.w\t@($disp8, pc), $Rn", []>;

// MOVA @(disp8*4,PC),R0  (1100 0111 dddd dddd)
let Defs = [R0] in
def MOVA : F_D8<0b11000111,
  (outs), (ins disp8_pcrel:$disp8),
  "mova\t@($disp8, pc), r0", []>;

//===----------------------------------------------------------------------===//
// MOV — Indirect (register base)
//===----------------------------------------------------------------------===//

// MOV.L @Rm,Rn   (0110 nnnn mmmm 0010)
def MOVL_IND : F_RR<0b0110, 0b0010,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "mov.l\t@$Rm, $Rn",
  [(set GPR:$Rn, (load GPR:$Rm))]>;

// MOV.L Rm,@Rn   (0010 nnnn mmmm 0010)
def MOVL_STO : F_RR<0b0010, 0b0010,
  (outs), (ins GPR:$Rm, GPR:$Rn),
  "mov.l\t$Rm, @$Rn",
  [(store GPR:$Rm, GPR:$Rn)]>;

// MOV.L @Rm+,Rn  (0110 nnnn mmmm 0110)
def MOVL_INC : F_RR<0b0110, 0b0110,
  (outs GPR:$Rn, GPR:$Rm_wb), (ins GPR:$Rm),
  "mov.l\t@${Rm}+, $Rn", []> { let Constraints = "$Rm = $Rm_wb"; }

// MOV.L Rm,@-Rn  (0010 nnnn mmmm 0110)
def MOVL_DEC : F_RR<0b0010, 0b0110,
  (outs GPR:$Rn_wb), (ins GPR:$Rm, GPR:$Rn),
  "mov.l\t$Rm, @-$Rn", []> { let Constraints = "$Rn = $Rn_wb"; }

// MOV.W @Rm,Rn   (0110 nnnn mmmm 0001)
def MOVW_IND : F_RR<0b0110, 0b0001,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "mov.w\t@$Rm, $Rn",
  [(set GPR:$Rn, (sextloadi16 GPR:$Rm))]>;
def : Pat<(extloadi16 GPR:$Rm), (MOVW_IND GPR:$Rm)>;

// MOV.W Rm,@Rn   (0010 nnnn mmmm 0001)
def MOVW_STO : F_RR<0b0010, 0b0001,
  (outs), (ins GPR:$Rm, GPR:$Rn),
  "mov.w\t$Rm, @$Rn",
  [(truncstorei16 GPR:$Rm, GPR:$Rn)]>;

// MOV.W @Rm+,Rn  (0110 nnnn mmmm 0101)
def MOVW_INC : F_RR<0b0110, 0b0101,
  (outs GPR:$Rn, GPR:$Rm_wb), (ins GPR:$Rm),
  "mov.w\t@${Rm}+, $Rn", []> { let Constraints = "$Rm = $Rm_wb"; }

// MOV.W Rm,@-Rn  (0010 nnnn mmmm 0101)
def MOVW_DEC : F_RR<0b0010, 0b0101,
  (outs GPR:$Rn_wb), (ins GPR:$Rm, GPR:$Rn),
  "mov.w\t$Rm, @-$Rn", []> { let Constraints = "$Rn = $Rn_wb"; }

// MOV.B @Rm,Rn   (0110 nnnn mmmm 0000)
def MOVB_IND : F_RR<0b0110, 0b0000,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "mov.b\t@$Rm, $Rn",
  [(set GPR:$Rn, (sextloadi8 GPR:$Rm))]>;
def : Pat<(extloadi8 GPR:$Rm), (MOVB_IND GPR:$Rm)>;

// MOV.B Rm,@Rn   (0010 nnnn mmmm 0000)
def MOVB_STO : F_RR<0b0010, 0b0000,
  (outs), (ins GPR:$Rm, GPR:$Rn),
  "mov.b\t$Rm, @$Rn",
  [(truncstorei8 GPR:$Rm, GPR:$Rn)]>;

// MOV.B @Rm+,Rn  (0110 nnnn mmmm 0100)
def MOVB_INC : F_RR<0b0110, 0b0100,
  (outs GPR:$Rn, GPR:$Rm_wb), (ins GPR:$Rm),
  "mov.b\t@${Rm}+, $Rn", []> { let Constraints = "$Rm = $Rm_wb"; }

// MOV.B Rm,@-Rn  (0010 nnnn mmmm 0100)
def MOVB_DEC : F_RR<0b0010, 0b0100,
  (outs GPR:$Rn_wb), (ins GPR:$Rm, GPR:$Rn),
  "mov.b\t$Rm, @-$Rn", []> { let Constraints = "$Rn = $Rn_wb"; }

//===----------------------------------------------------------------------===//
// Constant Pool Pseudo
//===----------------------------------------------------------------------===//
def cpinst_operand : Operand<i32>;

let isNotDuplicable = 1 in
def CONSTPOOL_ENTRY :
  SHPseudo<(outs), (ins cpinst_operand:$instid, cpinst_operand:$cpidx,
                      i32imm:$size),
         "CONSTPOOL_ENTRY\t$instid, $cpidx, $size", []>;

//===----------------------------------------------------------------------===//
// FrameIndex Pseudo
//===----------------------------------------------------------------------===//
let isCodeGenOnly = 1 in
def LEA_FI : SHPseudo<(outs GPR:$dst), (ins i32imm:$fi, i32imm:$off),
                       "lea.fi\t$dst, $fi, $off",
                       [(set GPR:$dst, (add tframeindex:$fi, imm:$off))]>;

def to_tframeindex : SDNodeXForm<frameindex, [{
  return CurDAG->getTargetFrameIndex(N->getIndex(), N->getValueType(0));
}]>;
def : Pat<(frameindex:$fi), (LEA_FI (i32 (to_tframeindex $fi)), 0)>;

// FP spill/fill pseudo instructions.
// These mirror MOVL_STO/MOVL_IND but operate on FPR32 registers.
// eliminateFrameIndex resolves the frame index operand.
let isCodeGenOnly = 1, mayStore = 1, Uses = [FPSCR] in
def FMOV_S_SPILL : SHPseudo<(outs), (ins FPR32:$FR, GPR:$base, i32imm:$off),
                              "fmov.s\t$FR, @$base /* spill */", []>;

let isCodeGenOnly = 1, mayLoad = 1, Uses = [FPSCR] in
def FMOV_S_FILL : SHPseudo<(outs FPR32:$FR), (ins GPR:$base, i32imm:$off),
                             "fmov.s\t@$base, $FR /* fill */", []>;

// FP double-precision spill/fill pseudo instructions.
// Expanded by eliminateFrameIndex into two FMOV_S operations
// with correct LE word ordering (LSW at lower address).
let isCodeGenOnly = 1, mayStore = 1, Uses = [FPSCR] in
def FMOV_D_SPILL : SHPseudo<(outs), (ins FPR64:$DR, GPR:$base, i32imm:$off),
                              "fmov.d\t$DR, @$base /* spill */", []>;

let isCodeGenOnly = 1, mayLoad = 1, Uses = [FPSCR] in
def FMOV_D_FILL : SHPseudo<(outs FPR64:$DR), (ins GPR:$base, i32imm:$off),
                             "fmov.d\t@$base, $DR /* fill */", []>;

//===----------------------------------------------------------------------===//
// SH4A Atomics
//===----------------------------------------------------------------------===//
let Predicates = [HasSH4A] in {
  let Defs = [R0], mayLoad = 1 in
  def MOVLI_L : F_R<0b0000, 0b01100011,
    (outs), (ins GPR:$Rn),
    "movli.l\t@$Rn, r0", []>;

  let Uses = [R0], Defs = [SR], mayStore = 1 in
  def MOVCO_L : F_R<0b0000, 0b01110011,
    (outs), (ins GPR:$Rn),
    "movco.l\tr0, @$Rn", []>;
}

//===----------------------------------------------------------------------===//
// MOV — R0-indexed indirect
//===----------------------------------------------------------------------===//

// MOV.L @(R0,Rm),Rn  (0000 nnnn mmmm 1110)
let Uses = [R0] in
def MOVL_R0IND : F_RR<0b0000, 0b1110,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "mov.l\t@(r0, ${Rm}), $Rn",
  [(set GPR:$Rn, (load (add GPR:$Rm, R0)))]>;

// MOV.L Rm,@(R0,Rn)  (0000 nnnn mmmm 0110)
let Uses = [R0] in
def MOVL_R0STO : F_RR<0b0000, 0b0110,
  (outs), (ins GPR:$Rm, GPR:$Rn),
  "mov.l\t$Rm, @(r0, ${Rn})",
  [(store GPR:$Rm, (add GPR:$Rn, R0))]>;

// MOV.W @(R0,Rm),Rn  (0000 nnnn mmmm 1101)
let Uses = [R0] in
def MOVW_R0IND : F_RR<0b0000, 0b1101,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "mov.w\t@(r0, ${Rm}), $Rn",
  [(set GPR:$Rn, (sextloadi16 (add GPR:$Rm, R0)))]>;

// MOV.W Rm,@(R0,Rn)  (0000 nnnn mmmm 0101)
let Uses = [R0] in
def MOVW_R0STO : F_RR<0b0000, 0b0101,
  (outs), (ins GPR:$Rm, GPR:$Rn),
  "mov.w\t$Rm, @(r0, ${Rn})",
  [(truncstorei16 GPR:$Rm, (add GPR:$Rn, R0))]>;

// MOV.B @(R0,Rm),Rn  (0000 nnnn mmmm 1100)
let Uses = [R0] in
def MOVB_R0IND : F_RR<0b0000, 0b1100,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "mov.b\t@(r0, ${Rm}), $Rn",
  [(set GPR:$Rn, (sextloadi8 (add GPR:$Rm, R0)))]>;

// MOV.B Rm,@(R0,Rn)  (0000 nnnn mmmm 0100)
let Uses = [R0] in
def MOVB_R0STO : F_RR<0b0000, 0b0100,
  (outs), (ins GPR:$Rm, GPR:$Rn),
  "mov.b\t$Rm, @(r0, ${Rn})",
  [(truncstorei8 GPR:$Rm, (add GPR:$Rn, R0))]>;

//===----------------------------------------------------------------------===//
// MOV — 4-bit displacement
//===----------------------------------------------------------------------===//

// MOV.L @(disp4*4,Rm),Rn  (0101 nnnn mmmm dddd)
def MOVL_DISP4_LD : F_RRD4<0b0101,
  (outs GPR:$Rn), (ins GPR:$Rm, uimm4:$disp4),
  "mov.l\t@($disp4, ${Rm}), $Rn", []>;

// MOV.L Rm,@(disp4*4,Rn)  (0001 nnnn mmmm dddd)
def MOVL_DISP4_ST : F_RRD4<0b0001,
  (outs), (ins GPR:$Rm, GPR:$Rn, uimm4:$disp4),
  "mov.l\t$Rm, @($disp4, ${Rn})", []>;

// MOV.W @(disp4*2,Rm),R0  (1000 0101 mmmm dddd)
let Defs = [R0] in
def MOVW_DISP4_LD : F_R_D4<0b10000101,
  (outs), (ins GPR:$Rm, uimm4:$disp4),
  "mov.w\t@($disp4, ${Rm}), r0", []>;

// MOV.W R0,@(disp4*2,Rn)  (1000 0001 nnnn dddd)
let Uses = [R0] in
def MOVW_DISP4_ST : F_R_D4<0b10000001,
  (outs), (ins GPR:$Rm, uimm4:$disp4),
  "mov.w\tr0, @($disp4, ${Rm})", []>;

// MOV.B @(disp4,Rm),R0    (1000 0100 mmmm dddd)
let Defs = [R0] in
def MOVB_DISP4_LD : F_R_D4<0b10000100,
  (outs), (ins GPR:$Rm, uimm4:$disp4),
  "mov.b\t@($disp4, ${Rm}), r0", []>;

// MOV.B R0,@(disp4,Rn)    (1000 0000 nnnn dddd)
let Uses = [R0] in
def MOVB_DISP4_ST : F_R_D4<0b10000000,
  (outs), (ins GPR:$Rm, uimm4:$disp4),
  "mov.b\tr0, @($disp4, ${Rm})", []>;

//===----------------------------------------------------------------------===//
// MOV — GBR-relative
//===----------------------------------------------------------------------===//

// MOV.L @(disp8*4,GBR),R0  (1100 0110 dddd dddd)
let Defs = [R0], Uses = [GBR] in
def MOVL_GBRD : F_D8<0b11000110,
  (outs), (ins uimm8:$disp8),
  "mov.l\t@($disp8, gbr), r0", []>;

// MOV.W @(disp8*2,GBR),R0  (1100 0101 dddd dddd)
let Defs = [R0], Uses = [GBR] in
def MOVW_GBRD : F_D8<0b11000101,
  (outs), (ins uimm8:$disp8),
  "mov.w\t@($disp8, gbr), r0", []>;

// MOV.B @(disp8,GBR),R0    (1100 0100 dddd dddd)
let Defs = [R0], Uses = [GBR] in
def MOVB_GBRD : F_D8<0b11000100,
  (outs), (ins uimm8:$disp8),
  "mov.b\t@($disp8, gbr), r0", []>;

// MOV.L R0,@(disp8*4,GBR)  (1100 0010 dddd dddd)
let Uses = [R0, GBR] in
def MOVL_GBRS : F_D8<0b11000010,
  (outs), (ins uimm8:$disp8),
  "mov.l\tr0, @($disp8, gbr)", []>;

// MOV.W R0,@(disp8*2,GBR)  (1100 0001 dddd dddd)
let Uses = [R0, GBR] in
def MOVW_GBRS : F_D8<0b11000001,
  (outs), (ins uimm8:$disp8),
  "mov.w\tr0, @($disp8, gbr)", []>;

// MOV.B R0,@(disp8,GBR)    (1100 0000 dddd dddd)
let Uses = [R0, GBR] in
def MOVB_GBRS : F_D8<0b11000000,
  (outs), (ins uimm8:$disp8),
  "mov.b\tr0, @($disp8, gbr)", []>;

// MOVCA.L R0,@Rn  (0000 nnnn 1100 0011)
let Uses = [R0], mayStore = 1 in
def MOVCA_L : F_R<0b0000, 0b11000011,
  (outs), (ins GPR:$Rn),
  "movca.l\tr0, @$Rn", []>;

//===----------------------------------------------------------------------===//
// Extend
//===----------------------------------------------------------------------===//

def EXTU_B : F_RR<0b0110, 0b1100,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "extu.b\t$Rm, $Rn",
  [(set GPR:$Rn, (and GPR:$Rm, 0xff))]>;

def EXTU_W : F_RR<0b0110, 0b1101,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "extu.w\t$Rm, $Rn",
  [(set GPR:$Rn, (and GPR:$Rm, 0xffff))]>;

def EXTS_B : F_RR<0b0110, 0b1110,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "exts.b\t$Rm, $Rn",
  [(set GPR:$Rn, (sext_inreg GPR:$Rm, i8))]>;

def EXTS_W : F_RR<0b0110, 0b1111,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "exts.w\t$Rm, $Rn",
  [(set GPR:$Rn, (sext_inreg GPR:$Rm, i16))]>;

//===----------------------------------------------------------------------===//
// SWAP / XTRCT
//===----------------------------------------------------------------------===//

// SWAP.B Rm,Rn  — swap bytes 0 and 1  (0110 nnnn mmmm 1000)
def SWAP_B : F_RR<0b0110, 0b1000,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "swap.b\t$Rm, $Rn", []>;

// SWAP.W Rm,Rn  — swap upper/lower halfwords  (0110 nnnn mmmm 1001)
def SWAP_W : F_RR<0b0110, 0b1001,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "swap.w\t$Rm, $Rn",
  [(set GPR:$Rn, (rotr GPR:$Rm, (i32 16)))]>;

// XTRCT Rm,Rn  — extract middle 32 bits  (0010 nnnn mmmm 1101)
def XTRCT : F_RR<0b0010, 0b1101,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "xtrct\t$Rm, $Rn", []> { let Constraints = "$Rn_src = $Rn"; }

//===----------------------------------------------------------------------===//
// Arithmetic
//===----------------------------------------------------------------------===//

// ADD Rm,Rn  (0011 nnnn mmmm 1100)
def ADD_RR : F_RR<0b0011, 0b1100,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "add\t$Rm, $Rn",
  [(set GPR:$Rn, (add GPR:$Rn_src, GPR:$Rm))]> {
  let Constraints = "$Rn_src = $Rn";
}

// ADD #imm8,Rn  (0111 nnnn iiii iiii)
def ADD_I8 : F_RI8<0b0111,
  (outs GPR:$Rn), (ins GPR:$Rn_src, simm8:$imm8),
  "add\t#$imm8, $Rn",
  [(set GPR:$Rn, (add GPR:$Rn_src, simm8:$imm8))]> {
  let Constraints = "$Rn_src = $Rn";
}

// ADDC Rm,Rn  — add with carry  (0011 nnnn mmmm 1110)
let Defs = [SR], Uses = [SR] in
def ADDC : F_RR<0b0011, 0b1110,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "addc\t$Rm, $Rn", []> { let Constraints = "$Rn_src = $Rn"; }

// ADDV Rm,Rn  — add with overflow  (0011 nnnn mmmm 1111)
let Defs = [SR] in
def ADDV : F_RR<0b0011, 0b1111,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "addv\t$Rm, $Rn", []> { let Constraints = "$Rn_src = $Rn"; }

// SUB Rm,Rn  (0011 nnnn mmmm 1000)
def SUB_RR : F_RR<0b0011, 0b1000,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "sub\t$Rm, $Rn",
  [(set GPR:$Rn, (sub GPR:$Rn_src, GPR:$Rm))]> {
  let Constraints = "$Rn_src = $Rn";
}

// SUBC Rm,Rn  — subtract with borrow  (0011 nnnn mmmm 1010)
let Defs = [SR], Uses = [SR] in
def SUBC : F_RR<0b0011, 0b1010,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "subc\t$Rm, $Rn", []> { let Constraints = "$Rn_src = $Rn"; }

// SUBV Rm,Rn  — subtract with overflow  (0011 nnnn mmmm 1011)
let Defs = [SR] in
def SUBV : F_RR<0b0011, 0b1011,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "subv\t$Rm, $Rn", []> { let Constraints = "$Rn_src = $Rn"; }

// NEG Rm,Rn  (0110 nnnn mmmm 1011)
def NEG_RR : F_RR<0b0110, 0b1011,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "neg\t$Rm, $Rn",
  [(set GPR:$Rn, (ineg GPR:$Rm))]>;

// NEGC Rm,Rn  — negate with borrow  (0110 nnnn mmmm 1010)
let Defs = [SR], Uses = [SR] in
def NEGC : F_RR<0b0110, 0b1010,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "negc\t$Rm, $Rn", []>;

// NOT Rm,Rn  (0110 nnnn mmmm 0111)
def NOT_RR : F_RR<0b0110, 0b0111,
  (outs GPR:$Rn), (ins GPR:$Rm),
  "not\t$Rm, $Rn",
  [(set GPR:$Rn, (not GPR:$Rm))]>;

//===----------------------------------------------------------------------===//
// Logic
//===----------------------------------------------------------------------===//

// AND Rm,Rn  (0010 nnnn mmmm 1001)
def AND_RR : F_RR<0b0010, 0b1001,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "and\t$Rm, $Rn",
  [(set GPR:$Rn, (and GPR:$Rn_src, GPR:$Rm))]> {
  let Constraints = "$Rn_src = $Rn";
}

// OR Rm,Rn  (0010 nnnn mmmm 1011)
def OR_RR : F_RR<0b0010, 0b1011,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "or\t$Rm, $Rn",
  [(set GPR:$Rn, (or GPR:$Rn_src, GPR:$Rm))]> {
  let Constraints = "$Rn_src = $Rn";
}

// XOR Rm,Rn  (0010 nnnn mmmm 1010)
def XOR_RR : F_RR<0b0010, 0b1010,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "xor\t$Rm, $Rn",
  [(set GPR:$Rn, (xor GPR:$Rn_src, GPR:$Rm))]> {
  let Constraints = "$Rn_src = $Rn";
}

// TST Rm,Rn  (0010 nnnn mmmm 1000)
let Defs = [SR] in
def TST_RR : F_RR<0b0010, 0b1000,
  (outs), (ins GPR:$Rn, GPR:$Rm),
  "tst\t$Rm, $Rn", []>;

// CMP/STR Rm,Rn  — T=1 if any byte equal  (0010 nnnn mmmm 1100)
let Defs = [SR] in
def CMP_STR : F_RR<0b0010, 0b1100,
  (outs), (ins GPR:$Rn, GPR:$Rm),
  "cmp/str\t$Rm, $Rn", []>;

//===----------------------------------------------------------------------===//
// Logic with Immediate (R0-implicit)
//===----------------------------------------------------------------------===//

// TST #imm8,R0  (1100 1000 iiii iiii)
let Defs = [SR], Uses = [R0] in
def TST_I8 : F_I8<0b11001000,
  (outs), (ins uimm8:$imm8),
  "tst\t#$imm8, r0", []>;

// AND #imm8,R0  (1100 1001 iiii iiii)
let Defs = [R0], Uses = [R0] in
def AND_I8 : F_I8<0b11001001,
  (outs), (ins uimm8:$imm8),
  "and\t#$imm8, r0", []>;

// OR #imm8,R0   (1100 1011 iiii iiii)
let Defs = [R0], Uses = [R0] in
def OR_I8 : F_I8<0b11001011,
  (outs), (ins uimm8:$imm8),
  "or\t#$imm8, r0", []>;

// XOR #imm8,R0  (1100 1010 iiii iiii)
let Defs = [R0], Uses = [R0] in
def XOR_I8 : F_I8<0b11001010,
  (outs), (ins uimm8:$imm8),
  "xor\t#$imm8, r0", []>;

// TST.B #imm8,@(R0,GBR)  (1100 1100 iiii iiii)
let Defs = [SR], Uses = [R0, GBR] in
def TST_B : F_I8<0b11001100,
  (outs), (ins uimm8:$imm8),
  "tst.b\t#$imm8, @(r0, gbr)", []>;

// AND.B #imm8,@(R0,GBR)  (1100 1101 iiii iiii)
let Uses = [R0, GBR] in
def AND_B : F_I8<0b11001101,
  (outs), (ins uimm8:$imm8),
  "and.b\t#$imm8, @(r0, gbr)", []>;

// OR.B #imm8,@(R0,GBR)   (1100 1111 iiii iiii)
let Uses = [R0, GBR] in
def OR_B : F_I8<0b11001111,
  (outs), (ins uimm8:$imm8),
  "or.b\t#$imm8, @(r0, gbr)", []>;

// XOR.B #imm8,@(R0,GBR)  (1100 1110 iiii iiii)
let Uses = [R0, GBR] in
def XOR_B : F_I8<0b11001110,
  (outs), (ins uimm8:$imm8),
  "xor.b\t#$imm8, @(r0, gbr)", []>;

// TAS.B @Rn  — test-and-set byte, sets T  (0100 nnnn 0001 1011)
let Defs = [SR] in
def TAS_B : F_R<0b0100, 0b00011011,
  (outs), (ins GPR:$Rn),
  "tas.b\t@$Rn", []>;

// SH shift-by-1 instructions shift a bit into the T register:
// SHLL: MSB → T, SHLR: LSB → T, SHAR: LSB → T, SHAL: MSB → T
// They must declare Defs = [SR] so the scheduler/branch-folder knows
// they clobber the T-bit and won't reorder them before BT/BF branches.

let Defs = [SR] in {

def SHLL : F_R<0b0100, 0b00000000,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "shll\t$Rn",
  [(set GPR:$Rn, (shl GPR:$Rn_src, (i32 1)))]> {
  let Constraints = "$Rn_src = $Rn";
}

def SHLR : F_R<0b0100, 0b00000001,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "shlr\t$Rn",
  [(set GPR:$Rn, (srl GPR:$Rn_src, (i32 1)))]> {
  let Constraints = "$Rn_src = $Rn";
}

// SHAL Rn  — shift arithmetic left, MSB→T  (0100 nnnn 0010 0000)
def SHAL : F_R<0b0100, 0b00100000,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "shal\t$Rn", []> { let Constraints = "$Rn_src = $Rn"; }

def SHAR : F_R<0b0100, 0b00100001,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "shar\t$Rn",
  [(set GPR:$Rn, (sra GPR:$Rn_src, (i32 1)))]> {
  let Constraints = "$Rn_src = $Rn";
}

} // let Defs = [SR]

def SHLL2 : F_R<0b0100, 0b00001000,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "shll2\t$Rn",
  [(set GPR:$Rn, (shl GPR:$Rn_src, (i32 2)))]> {
  let Constraints = "$Rn_src = $Rn";
}

def SHLR2 : F_R<0b0100, 0b00001001,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "shlr2\t$Rn",
  [(set GPR:$Rn, (srl GPR:$Rn_src, (i32 2)))]> {
  let Constraints = "$Rn_src = $Rn";
}

def SHLL8 : F_R<0b0100, 0b00011000,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "shll8\t$Rn",
  [(set GPR:$Rn, (shl GPR:$Rn_src, (i32 8)))]> {
  let Constraints = "$Rn_src = $Rn";
}

def SHLR8 : F_R<0b0100, 0b00011001,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "shlr8\t$Rn",
  [(set GPR:$Rn, (srl GPR:$Rn_src, (i32 8)))]> {
  let Constraints = "$Rn_src = $Rn";
}

def SHLL16 : F_R<0b0100, 0b00101000,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "shll16\t$Rn",
  [(set GPR:$Rn, (shl GPR:$Rn_src, (i32 16)))]> {
  let Constraints = "$Rn_src = $Rn";
}

def SHLR16 : F_R<0b0100, 0b00101001,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "shlr16\t$Rn",
  [(set GPR:$Rn, (srl GPR:$Rn_src, (i32 16)))]> {
  let Constraints = "$Rn_src = $Rn";
}

let Constraints = "$Rn_src = $Rn" in {
  def SHLD : F_RR<0b0100, 0b1101,
      (outs GPR:$Rn), (ins GPR:$Rm, GPR:$Rn_src),
      "shld\t$Rm, $Rn", []>;

  def SHAD : F_RR<0b0100, 0b1100,
      (outs GPR:$Rn), (ins GPR:$Rm, GPR:$Rn_src),
      "shad\t$Rm, $Rn", []>;
}

def : Pat<(shl GPR:$Rn, GPR:$Rm), (SHLD GPR:$Rm, GPR:$Rn)>;
def : Pat<(srl GPR:$Rn, GPR:$Rm), (SHLD (NEG_RR GPR:$Rm), GPR:$Rn)>;
def : Pat<(sra GPR:$Rn, GPR:$Rm), (SHAD (NEG_RR GPR:$Rm), GPR:$Rn)>;

// BSWAP is implemented with a sequence of SWAP_B and SWAP_W
def : Pat<(bswap GPR:$Rm), (SWAP_B (SWAP_W (SWAP_B GPR:$Rm)))>;

// Shift amounts will be auto-materialized by ISel into registers

//===----------------------------------------------------------------------===//
// Rotate
//===----------------------------------------------------------------------===//

// ROTL Rn  — rotate left, MSB→T  (0100 nnnn 0000 0100)
let Defs = [SR] in
def ROTL : F_R<0b0100, 0b00000100,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "rotl\t$Rn", []> { let Constraints = "$Rn_src = $Rn"; }

// ROTR Rn  — rotate right, LSB→T  (0100 nnnn 0000 0101)
let Defs = [SR] in
def ROTR : F_R<0b0100, 0b00000101,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "rotr\t$Rn", []> { let Constraints = "$Rn_src = $Rn"; }

// ROTCL Rn  — rotate left through T  (0100 nnnn 0010 0100)
let Defs = [SR], Uses = [SR] in
def ROTCL : F_R<0b0100, 0b00100100,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "rotcl\t$Rn", []> { let Constraints = "$Rn_src = $Rn"; }

// ROTCR Rn  — rotate right through T  (0100 nnnn 0010 0101)
let Defs = [SR], Uses = [SR] in
def ROTCR : F_R<0b0100, 0b00100101,
  (outs GPR:$Rn), (ins GPR:$Rn_src),
  "rotcr\t$Rn", []> { let Constraints = "$Rn_src = $Rn"; }

//===----------------------------------------------------------------------===//
// Compare
//===----------------------------------------------------------------------===//

let Defs = [SR] in {
  def CMP_EQ  : F_RR<0b0011, 0b0000, (outs), (ins GPR:$Rn, GPR:$Rm),
    "cmp/eq\t$Rm, $Rn", []>;
  def CMP_GT  : F_RR<0b0011, 0b0111, (outs), (ins GPR:$Rn, GPR:$Rm),
    "cmp/gt\t$Rm, $Rn", []>;
  def CMP_GE  : F_RR<0b0011, 0b0011, (outs), (ins GPR:$Rn, GPR:$Rm),
    "cmp/ge\t$Rm, $Rn", []>;
  def CMP_HI  : F_RR<0b0011, 0b0110, (outs), (ins GPR:$Rn, GPR:$Rm),
    "cmp/hi\t$Rm, $Rn", []>;
  def CMP_HS  : F_RR<0b0011, 0b0010, (outs), (ins GPR:$Rn, GPR:$Rm),
    "cmp/hs\t$Rm, $Rn", []>;
  def CMP_PZ  : F_R<0b0100, 0b00010001, (outs), (ins GPR:$Rn),
    "cmp/pz\t$Rn", []>;
  def CMP_PL  : F_R<0b0100, 0b00010101, (outs), (ins GPR:$Rn),
    "cmp/pl\t$Rn", []>;
  // DT Rn  — decrement and test: Rn -= 1, T=(Rn==0)  (0100 nnnn 0001 0000)
  def DT : F_R<0b0100, 0b00010000,
    (outs GPR:$Rn), (ins GPR:$Rn_src),
    "dt\t$Rn", []> { let Constraints = "$Rn_src = $Rn"; }
  // CMP/EQ #imm8,R0  (1000 1000 iiii iiii)
  def CMP_EQ_I8 : F_I8<0b10001000, (outs), (ins simm8:$imm8),
    "cmp/eq\t#$imm8, r0", []> { let Uses = [R0]; }
}

//===----------------------------------------------------------------------===//
// Division
//===----------------------------------------------------------------------===//

// DIV0U  — init unsigned division, clears T,M,Q  (0000 0000 0001 1001)
let Defs = [SR] in
def DIV0U : F_Bare<0b0000000000011001, (outs), (ins), "div0u", []>;

// DIV0S Rm,Rn  — init signed division  (0010 nnnn mmmm 0111)
let Defs = [SR] in
def DIV0S : F_RR<0b0010, 0b0111,
  (outs), (ins GPR:$Rn, GPR:$Rm),
  "div0s\t$Rm, $Rn", []>;

// DIV1 Rm,Rn  — one step of division  (0011 nnnn mmmm 0100)
let Defs = [SR], Uses = [SR] in
def DIV1 : F_RR<0b0011, 0b0100,
  (outs GPR:$Rn), (ins GPR:$Rn_src, GPR:$Rm),
  "div1\t$Rm, $Rn", []> { let Constraints = "$Rn_src = $Rn"; }

//===----------------------------------------------------------------------===//
// Multiply
//===----------------------------------------------------------------------===//

// MUL.L Rm,Rn  — 32×32→MACL  (0000 nnnn mmmm 0111)
let Defs = [MACL] in
def MUL_L : F_RR<0b0000, 0b0111,
  (outs), (ins GPR:$Rn, GPR:$Rm),
  "mul.l\t$Rm, $Rn", []>;

// MULS.W Rm,Rn  — 16×16 signed→MACL  (0010 nnnn mmmm 1111)
let Defs = [MACL] in
def MULS_W : F_RR<0b0010, 0b1111,
  (outs), (ins GPR:$Rn, GPR:$Rm),
  "muls.w\t$Rm, $Rn", []>;

// MULU.W Rm,Rn  — 16×16 unsigned→MACL  (0010 nnnn mmmm 1110)
let Defs = [MACL] in
def MULU_W : F_RR<0b0010, 0b1110,
  (outs), (ins GPR:$Rn, GPR:$Rm),
  "mulu.w\t$Rm, $Rn", []>;

// DMULS.L Rm,Rn  — 32×32 signed→MACH:MACL  (0011 nnnn mmmm 1101)
let Defs = [MACH, MACL] in
def DMULS_L : F_RR<0b0011, 0b1101,
  (outs), (ins GPR:$Rn, GPR:$Rm),
  "dmuls.l\t$Rm, $Rn", []>;

// DMULU.L Rm,Rn  — 32×32 unsigned→MACH:MACL  (0011 nnnn mmmm 0101)
let Defs = [MACH, MACL] in
def DMULU_L : F_RR<0b0011, 0b0101,
  (outs), (ins GPR:$Rn, GPR:$Rm),
  "dmulu.l\t$Rm, $Rn", []>;

// MAC.L @Rm+,@Rn+  — 32-bit MAC  (0000 nnnn mmmm 1111)
let Defs = [MACH, MACL], Uses = [MACH, MACL], mayLoad = 1 in
def MAC_L : F_RR<0b0000, 0b1111,
  (outs GPR:$Rn_wb, GPR:$Rm_wb), (ins GPR:$Rn, GPR:$Rm),
  "mac.l\t@${Rm}+, @${Rn}+", []> {
  let Constraints = "$Rn = $Rn_wb, $Rm = $Rm_wb";
}

// MAC.W @Rm+,@Rn+  — 16-bit MAC  (0100 nnnn mmmm 1111)
let Defs = [MACH, MACL], Uses = [MACH, MACL], mayLoad = 1 in
def MAC_W : F_RR<0b0100, 0b1111,
  (outs GPR:$Rn_wb, GPR:$Rm_wb), (ins GPR:$Rn, GPR:$Rm),
  "mac.w\t@${Rm}+, @${Rn}+", []> {
  let Constraints = "$Rn = $Rn_wb, $Rm = $Rm_wb";
}

// STS MACH,Rn  (0000 nnnn 0000 1010)
def STS_MACH : F_R<0b0000, 0b00001010,
  (outs GPR:$Rn), (ins), "sts\tmach, $Rn", []> { let Uses = [MACH]; }

// STS MACL,Rn  (0000 nnnn 0001 1010)
def STS_MACL : F_R<0b0000, 0b00011010,
  (outs GPR:$Rn), (ins), "sts\tmacl, $Rn", []> { let Uses = [MACL]; }

// 32-bit multiply pseudo (MUL.L + STS MACL)
let Defs = [MACL], Uses = [MACL] in
def MUL32_PSEUDO : SHPseudo<(outs GPR:$dst), (ins GPR:$Rn, GPR:$Rm),
  "mul.l\t$Rm, $Rn\n\tsts\tmacl, $dst",
  [(set GPR:$dst, (mul GPR:$Rn, GPR:$Rm))]>;

let Defs = [MACH, MACL], Uses = [MACH] in {
  def MULHS_PSEUDO : SHPseudo<(outs GPR:$dst), (ins GPR:$Rn, GPR:$Rm),
    "dmuls.l\t$Rm, $Rn\n\tsts\tmach, $dst",
    [(set GPR:$dst, (mulhs GPR:$Rn, GPR:$Rm))]>;

  def MULHU_PSEUDO : SHPseudo<(outs GPR:$dst), (ins GPR:$Rn, GPR:$Rm),
    "dmulu.l\t$Rm, $Rn\n\tsts\tmach, $dst",
    [(set GPR:$dst, (mulhu GPR:$Rn, GPR:$Rm))]>;
}

//===----------------------------------------------------------------------===//
// Branch
//===----------------------------------------------------------------------===//

let Uses = [SR] in {
  def BT : F_D8<0b10001001, (outs), (ins brtarget8:$disp8),
    "bt\t$disp8", []> { let isBranch = 1; let isTerminator = 1; }
  def BF : F_D8<0b10001011, (outs), (ins brtarget8:$disp8),
    "bf\t$disp8", []> { let isBranch = 1; let isTerminator = 1; }
  // BT/S — branch if T=1 with delay slot  (1000 1101 dddd dddd)
  def BT_S : F_D8<0b10001101, (outs), (ins brtarget8:$disp8),
    "bt/s\t$disp8", []> {
    let isBranch = 1; let isTerminator = 1; let hasDelaySlot = 1;
  }
  // BF/S — branch if T=0 with delay slot  (1000 1111 dddd dddd)
  def BF_S : F_D8<0b10001111, (outs), (ins brtarget8:$disp8),
    "bf/s\t$disp8", []> {
    let isBranch = 1; let isTerminator = 1; let hasDelaySlot = 1;
  }
}

//===----------------------------------------------------------------------===//
// Pseudo Instructions for SELECT
//===----------------------------------------------------------------------===//

let usesCustomInserter = 1 in {
  def Select_i32 : SHPseudo<(outs GPR:$dst),
                          (ins GPR:$T, GPR:$F, GPR:$cond),
                          "# Select_i32",
                          [(set i32:$dst, (select i32:$cond, i32:$T, i32:$F))]>;

  def Select_f32 : SHPseudo<(outs FPR32:$dst),
                          (ins FPR32:$T, FPR32:$F, GPR:$cond),
                          "# Select_f32",
                          [(set f32:$dst, (select i32:$cond, f32:$T, f32:$F))]>;

  def Select_f64 : SHPseudo<(outs FPR64:$dst),
                          (ins FPR64:$T, FPR64:$F, GPR:$cond),
                          "# Select_f64",
                          [(set f64:$dst, (select i32:$cond, f64:$T, f64:$F))]>;
}
// BRA label  — unconditional with delay slot  (1010 dddd dddd dddd)
def BRA : F_D12<0b1010,
  (outs), (ins brtarget12:$disp12),
  "bra\t$disp12",
  [(br bb:$disp12)]> {
  let isBranch = 1; let isTerminator = 1; let isBarrier = 1; let hasDelaySlot = 1;
}

// BR_FAR — far unconditional branch pseudo for the constant island pass.
// Expands in AsmPrinter to: push R0; mov.l @CPI, R0; jmp @R0; pop R0
// CPI operand points to SHConstantPoolMBB containing the target MBB address.
// Total size: 2 (push) + 2 (mov.l) + 2 (jmp) + 2 (pop) = 8 bytes (fixed)
let isBranch = 1, isTerminator = 1, isBarrier = 1, isNotDuplicable = 1 in
def BR_FAR : SHPseudo<(outs), (ins brtarget12:$dst, i32imm:$cpi),
                        "# BR_FAR\t$dst", []>;

// COND_BR_FAR_T/F — far conditional branch pseudos.
// Expands in AsmPrinter to: BF/BT .+10; push R0; mov.l @CPI, R0; jmp @R0; pop R0
// The inverted short branch skips the long jump (8 bytes + 2 for BF/BT = disp 3).
// Total size: 2 (BF/BT) + 8 (BR_FAR expansion) = 10 bytes (fixed)
let isBranch = 1, isTerminator = 1, isNotDuplicable = 1 in {
  def COND_BR_FAR_T : SHPseudo<(outs), (ins brtarget12:$dst, i32imm:$cpi),
                                "# COND_BR_FAR_T\t$dst", []>;
  def COND_BR_FAR_F : SHPseudo<(outs), (ins brtarget12:$dst, i32imm:$cpi),
                                "# COND_BR_FAR_F\t$dst", []>;
}

// BSR label  — subroutine call with delay slot  (1011 dddd dddd dddd)
def BSR : F_D12<0b1011,
  (outs), (ins calltarget:$disp12),
  "bsr\t$disp12", []> {
  let isCall = 1; let hasDelaySlot = 1;
  let Defs = [PR, SR, R0, R1, R2, R3, R4, R5, R6, R7];
}

// Removed direct call patterns to avoid BSR overlows

// JMP @Rn  — indirect jump with delay slot  (0100 nnnn 0010 1011)
def JMP : F_R<0b0100, 0b00101011,
  (outs), (ins GPR:$Rn),
  "jmp\t@$Rn", [(brind GPR:$Rn)]> {
  let isBranch = 1; let isTerminator = 1; let isBarrier = 1;
  let isIndirectBranch = 1; let hasDelaySlot = 1;
}

// JSR @Rn  — indirect call with delay slot  (0100 nnnn 0000 1011)
def JSR : F_R<0b0100, 0b00001011,
  (outs), (ins GPR:$Rn),
  "jsr\t@$Rn",
  [(SHcallr GPR:$Rn)]> {
  let isCall = 1; let hasDelaySlot = 1;
  let Defs = [PR, SR, R0, R1, R2, R3, R4, R5, R6, R7];
}

// BRAF Rn  — branch far (PC+Rn+4), delay slot  (0000 nnnn 0010 0011)
def BRAF : F_R<0b0000, 0b00100011,
  (outs), (ins GPR:$Rn),
  "braf\t$Rn", []> {
  let isBranch = 1; let isTerminator = 1; let isBarrier = 1;
  let isIndirectBranch = 1; let hasDelaySlot = 1;
}

// BSRF Rn  — subroutine far (PC+Rn+4), delay slot  (0000 nnnn 0000 0011)
def BSRF : F_R<0b0000, 0b00000011,
  (outs), (ins GPR:$Rn),
  "bsrf\t$Rn", []> {
  let isCall = 1; let hasDelaySlot = 1;
  let Defs = [PR, SR, R0, R1, R2, R3, R4, R5, R6, R7];
}

// NOP  (0000 0000 0000 1001)
def NOP : F_Bare<0b0000000000001001, (outs), (ins), "nop", []>;

// RTS  — return from subroutine, delay slot  (0000 0000 0000 1011)
def RTS : F_Bare<0b0000000000001011,
  (outs), (ins), "rts",
  [(SHretflag)]> {
  let isTerminator = 1; let isReturn = 1; let isBarrier = 1;
  let hasDelaySlot = 1;
}

// BRCC pseudo (expands to CMP + BT/BF in SHInstrInfo.cpp)
let isBranch = 1, isTerminator = 1 in
def BRCC : SHPseudo<(outs),
  (ins GPR:$lhs, GPR:$rhs, i32imm:$cc, brtarget8:$dest),
  "# BRCC $dest",
  []>;

//===----------------------------------------------------------------------===//
// System Control
//===----------------------------------------------------------------------===//

let Defs = [SR] in {
  def CLRT  : F_Bare<0b0000000000001000, (outs), (ins), "clrt",  []>;
  def SETT  : F_Bare<0b0000000000011000, (outs), (ins), "sett",  []>;
  def CLRS  : F_Bare<0b0000000001001000, (outs), (ins), "clrs",  []>;
  def SETS  : F_Bare<0b0000000001011000, (outs), (ins), "sets",  []>;
}

let Defs = [MACH, MACL] in
def CLRMAC : F_Bare<0b0000000000101000, (outs), (ins), "clrmac", []>;

def SLEEP  : F_Bare<0b0000000000011011, (outs), (ins), "sleep",  []>;
def LDTLB  : F_Bare<0b0000000000111000, (outs), (ins), "ldtlb",  []>;

// RTE  — return from exception, delay slot  (0000 0000 0010 1011)
def RTE : F_Bare<0b0000000000101011,
  (outs), (ins), "rte", []> {
  let isTerminator = 1; let isReturn = 1; let isBarrier = 1;
  let hasDelaySlot = 1; let Defs = [SR];
}

// TRAPA #imm8  (1100 0011 iiii iiii)
def TRAPA : F_I8<0b11000011,
  (outs), (ins uimm8:$imm8),
  "trapa\t#$imm8", []>;

// Map ISD::TRAP to TRAPA #0
def : Pat<(trap), (TRAPA 0)>;

//===----------------------------------------------------------------------===//
// PR — Link Register
//===----------------------------------------------------------------------===//

def STS_PR : F_R<0b0000, 0b00101010,
  (outs GPR:$Rn), (ins), "sts\tpr, $Rn", []> { let Uses = [PR]; }

def LDS_PR : F_R<0b0100, 0b00101010,
  (outs), (ins GPR:$Rn), "lds\t$Rn, pr", []> { let Defs = [PR]; }

def STS_PR_DEC : F_R<0b0100, 0b00100010,
  (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "sts.l\tpr, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [PR];
}

def LDS_PR_INC : F_R<0b0100, 0b00100110,
  (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "lds.l\t@${Rn}+, pr", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [PR];
}

//===----------------------------------------------------------------------===//
// MACH / MACL Load/Store
//===----------------------------------------------------------------------===//

def LDS_MACH      : F_R<0b0100, 0b00001010, (outs), (ins GPR:$Rn),
  "lds\t$Rn, mach", []> { let Defs = [MACH]; }
def STS_MACH_DEC  : F_R<0b0100, 0b00000010, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "sts.l\tmach, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [MACH]; }
def LDS_MACH_INC  : F_R<0b0100, 0b00000110, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "lds.l\t@${Rn}+, mach", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [MACH]; }

def LDS_MACL      : F_R<0b0100, 0b00011010, (outs), (ins GPR:$Rn),
  "lds\t$Rn, macl", []> { let Defs = [MACL]; }
def STS_MACL_DEC  : F_R<0b0100, 0b00010010, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "sts.l\tmacl, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [MACL]; }
def LDS_MACL_INC  : F_R<0b0100, 0b00010110, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "lds.l\t@${Rn}+, macl", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [MACL]; }

//===----------------------------------------------------------------------===//
// FPUL Load/Store
//===----------------------------------------------------------------------===//

def STS_FPUL      : F_R<0b0000, 0b01011010, (outs GPR:$Rn), (ins),
  "sts\tfpul, $Rn", []> { let Uses = [FPUL]; }
def LDS_FPUL      : F_R<0b0100, 0b01011010, (outs), (ins GPR:$Rn),
  "lds\t$Rn, fpul", []> { let Defs = [FPUL]; }
def STS_FPUL_DEC  : F_R<0b0100, 0b01010010, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "sts.l\tfpul, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [FPUL]; }
def LDS_FPUL_INC  : F_R<0b0100, 0b01010110, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "lds.l\t@${Rn}+, fpul", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [FPUL]; }

//===----------------------------------------------------------------------===//
// FPSCR Load/Store
//===----------------------------------------------------------------------===//

def STS_FPSCR     : F_R<0b0000, 0b01101010, (outs GPR:$Rn), (ins),
  "sts\tfpscr, $Rn", []> { let Uses = [FPSCR]; }
def LDS_FPSCR     : F_R<0b0100, 0b01101010, (outs), (ins GPR:$Rn),
  "lds\t$Rn, fpscr", []> { let Defs = [FPSCR]; }
def STS_FPSCR_DEC : F_R<0b0100, 0b01100010, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "sts.l\tfpscr, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [FPSCR]; }
def LDS_FPSCR_INC : F_R<0b0100, 0b01100110, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "lds.l\t@${Rn}+, fpscr", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [FPSCR]; }

// FPCHG — toggle FPSCR.PR (single/double precision)  (1111 0111 1111 1101)
let Defs = [FPSCR], Uses = [FPSCR] in
def FPCHG : F_Bare<0xF7FD, (outs), (ins), "fpchg", []>;

//===----------------------------------------------------------------------===//
// STC / LDC — Control Registers
//===----------------------------------------------------------------------===//

// SR
def STC_SR      : F_R<0b0000, 0b00000010, (outs GPR:$Rn), (ins),
  "stc\tsr, $Rn",  []> { let Uses = [SR]; }
def LDC_SR      : F_R<0b0100, 0b00001110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, sr",  []> { let Defs = [SR]; }
def STC_SR_DEC  : F_R<0b0100, 0b00000011, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "stc.l\tsr, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [SR]; }
def LDC_SR_INC  : F_R<0b0100, 0b00000111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, sr", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [SR]; }

// GBR
def STC_GBR     : F_R<0b0000, 0b00010010, (outs GPR:$Rn), (ins),
  "stc\tgbr, $Rn",  []> { let Uses = [GBR]; }
def LDC_GBR     : F_R<0b0100, 0b00011110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, gbr",  []> { let Defs = [GBR]; }
def STC_GBR_DEC : F_R<0b0100, 0b00010011, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "stc.l\tgbr, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [GBR]; }
def LDC_GBR_INC : F_R<0b0100, 0b00010111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, gbr", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [GBR]; }

// VBR
def STC_VBR     : F_R<0b0000, 0b00100010, (outs GPR:$Rn), (ins),
  "stc\tvbr, $Rn",  []> { let Uses = [VBR]; }
def LDC_VBR     : F_R<0b0100, 0b00101110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, vbr",  []> { let Defs = [VBR]; }
def STC_VBR_DEC : F_R<0b0100, 0b00100011, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "stc.l\tvbr, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [VBR]; }
def LDC_VBR_INC : F_R<0b0100, 0b00100111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, vbr", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [VBR]; }

// SSR
def STC_SSR     : F_R<0b0000, 0b00110010, (outs GPR:$Rn), (ins),
  "stc\tssr, $Rn",  []> { let Uses = [SSR]; }
def LDC_SSR     : F_R<0b0100, 0b00111110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, ssr",  []> { let Defs = [SSR]; }
def STC_SSR_DEC : F_R<0b0100, 0b00110011, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "stc.l\tssr, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [SSR]; }
def LDC_SSR_INC : F_R<0b0100, 0b00110111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, ssr", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [SSR]; }

// SPC
def STC_SPC     : F_R<0b0000, 0b01000010, (outs GPR:$Rn), (ins),
  "stc\tspc, $Rn",  []> { let Uses = [SPC]; }
def LDC_SPC     : F_R<0b0100, 0b01001110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, spc",  []> { let Defs = [SPC]; }
def STC_SPC_DEC : F_R<0b0100, 0b01000011, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "stc.l\tspc, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [SPC]; }
def LDC_SPC_INC : F_R<0b0100, 0b01000111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, spc", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [SPC]; }

// DBR
def STC_DBR     : F_R<0b0000, 0b11111010, (outs GPR:$Rn), (ins),
  "stc\tdbr, $Rn",  []> { let Uses = [DBR]; }
def LDC_DBR     : F_R<0b0100, 0b11111010, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, dbr",  []> { let Defs = [DBR]; }
def STC_DBR_DEC : F_R<0b0100, 0b11110010, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "stc.l\tdbr, @-$Rn", []> {
  let mayStore = 1; let Constraints = "$Rn = $Rn_wb"; let Uses = [DBR]; }
def LDC_DBR_INC : F_R<0b0100, 0b11110110, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, dbr", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; let Defs = [DBR]; }

// Rn_BANK (banked registers, privileged)
// LDC Rm,Rn_BANK  (0100 mmmm 1nnn 1110)
def LDC_R0_BANK  : F_R<0b0100, 0b10001110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, r0_bank",  []>;
def LDC_R1_BANK  : F_R<0b0100, 0b10011110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, r1_bank",  []>;
def LDC_R2_BANK  : F_R<0b0100, 0b10101110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, r2_bank",  []>;
def LDC_R3_BANK  : F_R<0b0100, 0b10111110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, r3_bank",  []>;
def LDC_R4_BANK  : F_R<0b0100, 0b11001110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, r4_bank",  []>;
def LDC_R5_BANK  : F_R<0b0100, 0b11011110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, r5_bank",  []>;
def LDC_R6_BANK  : F_R<0b0100, 0b11101110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, r6_bank",  []>;
def LDC_R7_BANK  : F_R<0b0100, 0b11111110, (outs), (ins GPR:$Rn),
  "ldc\t$Rn, r7_bank",  []>;
// LDC.L @Rm+,Rn_BANK  (0100 mmmm 1nnn 0111)
def LDC_R0_BANK_INC : F_R<0b0100, 0b10000111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, r0_bank", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; }
def LDC_R1_BANK_INC : F_R<0b0100, 0b10010111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, r1_bank", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; }
def LDC_R2_BANK_INC : F_R<0b0100, 0b10100111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, r2_bank", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; }
def LDC_R3_BANK_INC : F_R<0b0100, 0b10110111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, r3_bank", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; }
def LDC_R4_BANK_INC : F_R<0b0100, 0b11000111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, r4_bank", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; }
def LDC_R5_BANK_INC : F_R<0b0100, 0b11010111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, r5_bank", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; }
def LDC_R6_BANK_INC : F_R<0b0100, 0b11100111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, r6_bank", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; }
def LDC_R7_BANK_INC : F_R<0b0100, 0b11110111, (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "ldc.l\t@${Rn}+, r7_bank", []> {
  let mayLoad = 1; let Constraints = "$Rn = $Rn_wb"; }


//===----------------------------------------------------------------------===//
// Cache Operations
//===----------------------------------------------------------------------===//

def OCBI  : F_R<0b0000, 0b10010011, (outs), (ins GPR:$Rn), "ocbi\t@$Rn",  []>;
def OCBP  : F_R<0b0000, 0b10100011, (outs), (ins GPR:$Rn), "ocbp\t@$Rn",  []>;
def OCBWB : F_R<0b0000, 0b10110011, (outs), (ins GPR:$Rn), "ocbwb\t@$Rn", []>;
def PREF  : F_R<0b0000, 0b10000011, (outs), (ins GPR:$Rn), "pref\t@$Rn",  []>;

let Predicates = [HasSH4A] in {
// ICBI @Rn — invalidate instruction cache block  (0000 nnnn 1110 0011)
def ICBI  : F_R<0b0000, 0b11100011, (outs), (ins GPR:$Rn), "icbi\t@$Rn",  []>;

// PREFI @Rn — prefetch instruction cache line  (0000 nnnn 1101 0011)
def PREFI : F_R<0b0000, 0b11010011, (outs), (ins GPR:$Rn), "prefi\t@$Rn", []>;

// MOVUA.L @Rn, R0 — unaligned 32-bit load  (0100 nnnn 1010 1001)
let Defs = [R0], mayLoad = 1 in
def MOVUA_L : F_R<0b0100, 0b10101001,
  (outs), (ins GPR:$Rn),
  "movua.l\t@$Rn, r0", []>;

// MOVUA.L @Rn+, R0 — unaligned 32-bit load, post-increment  (0100 nnnn 1110 1001)
let Defs = [R0], mayLoad = 1 in
def MOVUA_L_INC : F_R<0b0100, 0b11101001,
  (outs GPR:$Rn_wb), (ins GPR:$Rn),
  "movua.l\t@${Rn}+, r0", []> {
  let Constraints = "$Rn = $Rn_wb";
}

// SYNCO — synchronize data operations (memory barrier)  (0000 0000 1010 1011)
let hasSideEffects = 1 in
def SYNCO : F_Bare<0x00AB, (outs), (ins), "synco", []>;
} // Predicates = [HasSH4A]

//===----------------------------------------------------------------------===//
// Floating-Point — Single Precision (FPSCR.PR=0)
//===----------------------------------------------------------------------===//

let Uses = [FPSCR] in {

def FADD_S : F_FRR<0b0000,
  (outs FPR32:$FRn), (ins FPR32:$FRn_src, FPR32:$FRm),
  "fadd\t$FRm, $FRn",
  [(set FPR32:$FRn, (fadd FPR32:$FRn_src, FPR32:$FRm))]> {
  let Constraints = "$FRn_src = $FRn";
}

def FSUB_S : F_FRR<0b0001,
  (outs FPR32:$FRn), (ins FPR32:$FRn_src, FPR32:$FRm),
  "fsub\t$FRm, $FRn",
  [(set FPR32:$FRn, (fsub FPR32:$FRn_src, FPR32:$FRm))]> {
  let Constraints = "$FRn_src = $FRn";
}

def FMUL_S : F_FRR<0b0010,
  (outs FPR32:$FRn), (ins FPR32:$FRn_src, FPR32:$FRm),
  "fmul\t$FRm, $FRn",
  [(set FPR32:$FRn, (fmul FPR32:$FRn_src, FPR32:$FRm))]> {
  let Constraints = "$FRn_src = $FRn";
}

def FDIV_S : F_FRR<0b0011,
  (outs FPR32:$FRn), (ins FPR32:$FRn_src, FPR32:$FRm),
  "fdiv\t$FRm, $FRn",
  [(set FPR32:$FRn, (fdiv FPR32:$FRn_src, FPR32:$FRm))]> {
  let Constraints = "$FRn_src = $FRn";
}

let Defs = [SR] in {
  def FCMP_EQ_S : F_FRR<0b0100,
    (outs), (ins FPR32:$FRn, FPR32:$FRm),
    "fcmp/eq\t$FRm, $FRn", []>;
  def FCMP_GT_S : F_FRR<0b0101,
    (outs), (ins FPR32:$FRn, FPR32:$FRm),
    "fcmp/gt\t$FRm, $FRn", []>;
}

def FMOV_SS : F_FRR<0b1100,
  (outs FPR32:$FRn), (ins FPR32:$FRm),
  "fmov\t$FRm, $FRn", []>;

// FMAC FR0,FRm,FRn  (1111 nnnn mmmm 1110)
def FMAC_S : F_FRR<0b1110,
  (outs FPR32:$FRn), (ins FPR32:$FRn_src, FPR32:$FRm),
  "fmac\tfr0, $FRm, $FRn", []> {
  let Constraints = "$FRn_src = $FRn"; let Uses = [FR0, FPSCR];
}

def FABS_S : F_FR<0b1111, 0b01011101,
  (outs FPR32:$FRn), (ins FPR32:$FRn_src),
  "fabs\t$FRn",
  [(set FPR32:$FRn, (fabs FPR32:$FRn_src))]> {
  let Constraints = "$FRn_src = $FRn";
}

def FNEG_S : F_FR<0b1111, 0b01001101,
  (outs FPR32:$FRn), (ins FPR32:$FRn_src),
  "fneg\t$FRn",
  [(set FPR32:$FRn, (fneg FPR32:$FRn_src))]> {
  let Constraints = "$FRn_src = $FRn";
}

def FSQRT_S : F_FR<0b1111, 0b01101101,
  (outs FPR32:$FRn), (ins FPR32:$FRn_src),
  "fsqrt\t$FRn",
  [(set FPR32:$FRn, (fsqrt FPR32:$FRn_src))]> {
  let Constraints = "$FRn_src = $FRn";
}

// FSRRA FRn  — 1/sqrt(FRn)  (1111 nnnn 0111 1101)
def FSRRA_S : F_FR<0b1111, 0b01111101,
  (outs FPR32:$FRn), (ins FPR32:$FRn_src),
  "fsrra\t$FRn", []> { let Constraints = "$FRn_src = $FRn"; }

// FLDI0 FRn  — load 0.0  (1111 nnnn 1000 1101)
def FLDI0 : F_FR<0b1111, 0b10001101,
  (outs FPR32:$FRn), (ins),
  "fldi0\t$FRn", []>;

// FLDI1 FRn  — load 1.0  (1111 nnnn 1001 1101)
def FLDI1 : F_FR<0b1111, 0b10011101,
  (outs FPR32:$FRn), (ins),
  "fldi1\t$FRn", []>;

// FLOAT FPUL,FRn  (1111 nnnn 0010 1101)
def FLOAT_S : F_FR<0b1111, 0b00101101,
  (outs FPR32:$FRn), (ins),
  "float\tfpul, $FRn", []> { let Uses = [FPUL]; }

// FTRC FRn,FPUL  (1111 nnnn 0011 1101)
def FTRC_S : F_FR<0b1111, 0b00111101,
  (outs), (ins FPR32:$FRn),
  "ftrc\t$FRn, fpul", []> { let Defs = [FPUL]; }

// FLDS FRn,FPUL  (1111 nnnn 0001 1101)
def FLDS : F_FR<0b1111, 0b00011101,
  (outs), (ins FPR32:$FRn),
  "flds\t$FRn, fpul", []> { let Defs = [FPUL]; }

// FSTS FPUL,FRn  (1111 nnnn 0000 1101)
def FSTS : F_FR<0b1111, 0b00001101,
  (outs FPR32:$FRn), (ins),
  "fsts\tfpul, $FRn", []> { let Uses = [FPUL]; }

let Defs = [FPUL, FPSCR, R0], Uses = [FPUL] in {
  // These pseudos handle FPSCR.PR toggle inline using R0 as scratch.
  // expandPostRAPseudo sets PR=0 before the FP op and restores PR=1 after.
  def FP_TO_SINT_F32 : SHPseudo<(outs GPR:$dst), (ins FPR32:$src),
    "# fp_to_sint_f32",
    [(set GPR:$dst, (fp_to_sint FPR32:$src))]>;

  def SINT_TO_FP_F32 : SHPseudo<(outs FPR32:$dst), (ins GPR:$src),
    "# sint_to_fp_f32",
    [(set FPR32:$dst, (sint_to_fp GPR:$src))]>;

  def BITCAST_F32_TO_I32 : SHPseudo<(outs GPR:$dst), (ins FPR32:$src),
    "flds\t$src, fpul\n\tsts\tfpul, $dst",
    [(set GPR:$dst, (bitconvert FPR32:$src))]>;

  def BITCAST_I32_TO_F32 : SHPseudo<(outs FPR32:$dst), (ins GPR:$src),
    "lds\t$src, fpul\n\tfsts\tfpul, $dst",
    [(set FPR32:$dst, (bitconvert GPR:$src))]>;
}

} // end let Uses = [FPSCR]

//===----------------------------------------------------------------------===//
// FP Memory — Single Precision
//===----------------------------------------------------------------------===//

// FMOV.S @Rm,FRn        (1111 nnnn mmmm 1000)
let mayLoad = 1, Uses = [FPSCR] in
def FMOV_S_LD : F_FLD<0b1000,
  (outs FPR32:$FRn), (ins GPR:$Rm),
  "fmov.s\t@$Rm, $FRn",
  [(set FPR32:$FRn, (load GPR:$Rm))]>;

// FMOV.S @Rm+,FRn       (1111 nnnn mmmm 1001)
let mayLoad = 1, Uses = [FPSCR] in
def FMOV_S_INC : F_FLD<0b1001,
  (outs FPR32:$FRn, GPR:$Rm_wb), (ins GPR:$Rm),
  "fmov.s\t@${Rm}+, $FRn", []> { let Constraints = "$Rm = $Rm_wb"; }

// FMOV.S @(R0,Rm),FRn   (1111 nnnn mmmm 0110)
let mayLoad = 1, Uses = [R0, FPSCR] in
def FMOV_S_R0IND : F_FLD<0b0110,
  (outs FPR32:$FRn), (ins GPR:$Rm),
  "fmov.s\t@(r0, ${Rm}), $FRn",
  [(set FPR32:$FRn, (load (add GPR:$Rm, R0)))]>;

// FMOV.S FRm,@Rn        (1111 nnnn mmmm 1010)
let mayStore = 1, Uses = [FPSCR] in
def FMOV_S_ST : F_FST<0b1010,
  (outs), (ins GPR:$Rn, FPR32:$FRm),
  "fmov.s\t$FRm, @$Rn",
  [(store FPR32:$FRm, GPR:$Rn)]>;

// FMOV.S FRm,@-Rn       (1111 nnnn mmmm 1011)
let mayStore = 1, Uses = [FPSCR] in
def FMOV_S_DEC : F_FST<0b1011,
  (outs GPR:$Rn_wb), (ins GPR:$Rn, FPR32:$FRm),
  "fmov.s\t$FRm, @-$Rn", []> { let Constraints = "$Rn = $Rn_wb"; }

// FMOV.S FRm,@(R0,Rn)   (1111 nnnn mmmm 0111)
let mayStore = 1, Uses = [R0, FPSCR] in
def FMOV_S_R0STO : F_FST<0b0111,
  (outs), (ins GPR:$Rn, FPR32:$FRm),
  "fmov.s\t$FRm, @(r0, ${Rn})",
  [(store FPR32:$FRm, (add GPR:$Rn, R0))]>;

//===----------------------------------------------------------------------===//
// Floating-Point — Double Precision (FPSCR.PR=1)
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 1 in {
let Uses = [FPSCR] in {

def FADD_D : F_FRR<0b0000,
  (outs FPR64:$FRn), (ins FPR64:$FRn_src, FPR64:$FRm),
  "fadd\t$FRm, $FRn",
  [(set FPR64:$FRn, (fadd FPR64:$FRn_src, FPR64:$FRm))]> {
  let Constraints = "$FRn_src = $FRn";
}

def FSUB_D : F_FRR<0b0001,
  (outs FPR64:$FRn), (ins FPR64:$FRn_src, FPR64:$FRm),
  "fsub\t$FRm, $FRn",
  [(set FPR64:$FRn, (fsub FPR64:$FRn_src, FPR64:$FRm))]> {
  let Constraints = "$FRn_src = $FRn";
}

def FMUL_D : F_FRR<0b0010,
  (outs FPR64:$FRn), (ins FPR64:$FRn_src, FPR64:$FRm),
  "fmul\t$FRm, $FRn",
  [(set FPR64:$FRn, (fmul FPR64:$FRn_src, FPR64:$FRm))]> {
  let Constraints = "$FRn_src = $FRn";
}

def FDIV_D : F_FRR<0b0011,
  (outs FPR64:$FRn), (ins FPR64:$FRn_src, FPR64:$FRm),
  "fdiv\t$FRm, $FRn",
  [(set FPR64:$FRn, (fdiv FPR64:$FRn_src, FPR64:$FRm))]> {
  let Constraints = "$FRn_src = $FRn";
}

let Defs = [SR] in {
  def FCMP_EQ_D : F_FRR<0b0100,
    (outs), (ins FPR64:$FRn, FPR64:$FRm),
    "fcmp/eq\t$FRm, $FRn", []>;
  def FCMP_GT_D : F_FRR<0b0101,
    (outs), (ins FPR64:$FRn, FPR64:$FRm),
    "fcmp/gt\t$FRm, $FRn", []>;
}

def FMOV_DD : F_FRR<0b1100,
  (outs FPR64:$FRn), (ins FPR64:$FRm),
  "fmov\t$FRm, $FRn", []>;

def FABS_D : F_FR<0b1111, 0b01011101,
  (outs FPR64:$FRn), (ins FPR64:$FRn_src),
  "fabs\t$FRn",
  [(set FPR64:$FRn, (fabs FPR64:$FRn_src))]> {
  let Constraints = "$FRn_src = $FRn";
}

def FNEG_D : F_FR<0b1111, 0b01001101,
  (outs FPR64:$FRn), (ins FPR64:$FRn_src),
  "fneg\t$FRn",
  [(set FPR64:$FRn, (fneg FPR64:$FRn_src))]> {
  let Constraints = "$FRn_src = $FRn";
}

def FSQRT_D : F_FR<0b1111, 0b01101101,
  (outs FPR64:$FRn), (ins FPR64:$FRn_src),
  "fsqrt\t$FRn",
  [(set FPR64:$FRn, (fsqrt FPR64:$FRn_src))]> {
  let Constraints = "$FRn_src = $FRn";
}

// FLOAT FPUL,DRn  (1111 nnnn 0010 1101)
def FLOAT_D : F_FR<0b1111, 0b00101101,
  (outs FPR64:$FRn), (ins),
  "float\tfpul, $FRn", []> { let Uses = [FPUL]; }

// FTRC DRn,FPUL  (1111 nnnn 0011 1101)
def FTRC_D : F_FR<0b1111, 0b00111101,
  (outs), (ins FPR64:$FRn),
  "ftrc\t$FRn, fpul", []> { let Defs = [FPUL]; }

// FCNVDS DRn,FPUL  — double→single  (1111 nnnn 1011 1101)
def FCNVDS : F_FR<0b1111, 0b10111101,
  (outs), (ins FPR64:$FRn),
  "fcnvds\t$FRn, fpul", []> { let Defs = [FPUL]; }

// FCNVSD FPUL,DRn  — single→double  (1111 nnnn 1010 1101)
def FCNVSD : F_FR<0b1111, 0b10101101,
  (outs FPR64:$FRn), (ins),
  "fcnvsd\tfpul, $FRn", []> { let Uses = [FPUL]; }

// FSCA FPUL,DRn  — sin/cos  (1111 nnnn 1111 1101)
// FSCA is not FPSCR.PR-dependent — it always writes a DR pair.
// Moved outside isCodeGenOnly so the assembler can encode it.
let isCodeGenOnly = 0 in
def FSCA : F_FR<0b1111, 0b11111101,
  (outs FPR64:$FRn), (ins),
  "fsca\tfpul, $FRn", []> { let Uses = [FPUL]; }

let Defs = [FPUL], Uses = [FPUL] in {
  def FP_TO_SINT_F64 : SHPseudo<(outs GPR:$dst), (ins FPR64:$src),
    "ftrc\t$src, fpul\n\tsts\tfpul, $dst",
    [(set GPR:$dst, (fp_to_sint FPR64:$src))]>;

  def SINT_TO_FP_F64 : SHPseudo<(outs FPR64:$dst), (ins GPR:$src),
    "lds\t$src, fpul\n\tfloat\tfpul, $dst",
    [(set FPR64:$dst, (sint_to_fp GPR:$src))]>;

  def FP_EXTEND_F32_F64 : SHPseudo<(outs FPR64:$dst), (ins FPR32:$src),
    "flds\t$src, fpul\n\tfcnvsd\tfpul, $dst",
    [(set FPR64:$dst, (fpextend FPR32:$src))]>;

  def FP_ROUND_F64_F32 : SHPseudo<(outs FPR32:$dst), (ins FPR64:$src),
    "fcnvds\t$src, fpul\n\tfsts\tfpul, $dst",
    [(set FPR32:$dst, (fpround FPR64:$src))]>;
}

} // end let Uses = [FPSCR]

//===----------------------------------------------------------------------===//
// FP Memory — Double Precision
//===----------------------------------------------------------------------===//

// FMOV @Rm,DRn         (1111 nnnn mmmm 1000)
let mayLoad = 1, Uses = [FPSCR] in
def FMOV_D_LD : F_FLD<0b1000,
  (outs FPR64:$FRn), (ins GPR:$Rm),
  "fmov\t@$Rm, $FRn",
  [(set FPR64:$FRn, (load GPR:$Rm))]>;

// FMOV @Rm+,DRn        (1111 nnnn mmmm 1001)
let mayLoad = 1, Uses = [FPSCR] in
def FMOV_D_INC : F_FLD<0b1001,
  (outs FPR64:$FRn, GPR:$Rm_wb), (ins GPR:$Rm),
  "fmov\t@${Rm}+, $FRn", []> { let Constraints = "$Rm = $Rm_wb"; }

// FMOV @(R0,Rm),DRn    (1111 nnnn mmmm 0110)
let mayLoad = 1, Uses = [R0, FPSCR] in
def FMOV_D_R0IND : F_FLD<0b0110,
  (outs FPR64:$FRn), (ins GPR:$Rm),
  "fmov\t@(r0, ${Rm}), $FRn",
  [(set FPR64:$FRn, (load (add GPR:$Rm, R0)))]>;

// FMOV DRm,@Rn         (1111 nnnn mmmm 1010)
let mayStore = 1, Uses = [FPSCR] in
def FMOV_D_ST : F_FST<0b1010,
  (outs), (ins GPR:$Rn, FPR64:$FRm),
  "fmov\t$FRm, @$Rn",
  [(store FPR64:$FRm, GPR:$Rn)]>;

// FMOV DRm,@-Rn        (1111 nnnn mmmm 1011)
let mayStore = 1, Uses = [FPSCR] in
def FMOV_D_DEC : F_FST<0b1011,
  (outs GPR:$Rn_wb), (ins GPR:$Rn, FPR64:$FRm),
  "fmov\t$FRm, @-$Rn", []> { let Constraints = "$Rn = $Rn_wb"; }

// FMOV DRm,@(R0,Rn)    (1111 nnnn mmmm 0111)
let mayStore = 1, Uses = [R0, FPSCR] in
def FMOV_D_R0STO : F_FST<0b0111,
  (outs), (ins GPR:$Rn, FPR64:$FRm),
  "fmov\t$FRm, @(r0, ${Rn})",
  [(store FPR64:$FRm, (add GPR:$Rn, R0))]>;

//===----------------------------------------------------------------------===//
// FP — Cross-Bank XD (shadow double-precision access)
// XD registers use odd HW encodings (1,3,...,15) vs DR even (0,2,...,14),
// allowing the hardware to distinguish them in the same instruction field.
//===----------------------------------------------------------------------===//

// FMOV DRm,XDn  (1111 nnnn mmmm 1100)
let Uses = [FPSCR] in
def FMOV_DR_XD : F_FRR<0b1100,
  (outs XFPR64:$FRn), (ins FPR64:$FRm),
  "fmov\t$FRm, $FRn", []>;

// FMOV XDm,DRn  (1111 nnnn mmmm 1100)
let Uses = [FPSCR] in
def FMOV_XD_DR : F_FRR<0b1100,
  (outs FPR64:$FRn), (ins XFPR64:$FRm),
  "fmov\t$FRm, $FRn", []>;

// FMOV XDm,XDn  (1111 nnnn mmmm 1100)
let Uses = [FPSCR] in
def FMOV_XD : F_FRR<0b1100,
  (outs XFPR64:$FRn), (ins XFPR64:$FRm),
  "fmov\t$FRm, $FRn", []>;

// FMOV @Rm,XDn         (1111 nnnn mmmm 1000)
let mayLoad = 1, Uses = [FPSCR] in
def FMOV_XD_LD : F_FLD<0b1000,
  (outs XFPR64:$FRn), (ins GPR:$Rm),
  "fmov\t@$Rm, $FRn", []>;

// FMOV @Rm+,XDn        (1111 nnnn mmmm 1001)
let mayLoad = 1, Uses = [FPSCR] in
def FMOV_XD_INC : F_FLD<0b1001,
  (outs XFPR64:$FRn, GPR:$Rm_wb), (ins GPR:$Rm),
  "fmov\t@${Rm}+, $FRn", []> { let Constraints = "$Rm = $Rm_wb"; }

// FMOV @(R0,Rm),XDn    (1111 nnnn mmmm 0110)
let mayLoad = 1, Uses = [R0, FPSCR] in
def FMOV_XD_R0IND : F_FLD<0b0110,
  (outs XFPR64:$FRn), (ins GPR:$Rm),
  "fmov\t@(r0, ${Rm}), $FRn", []>;

// FMOV XDm,@Rn         (1111 nnnn mmmm 1010)
let mayStore = 1, Uses = [FPSCR] in
def FMOV_XD_ST : F_FST<0b1010,
  (outs), (ins GPR:$Rn, XFPR64:$FRm),
  "fmov\t$FRm, @$Rn", []>;

// FMOV XDm,@-Rn        (1111 nnnn mmmm 1011)
let mayStore = 1, Uses = [FPSCR] in
def FMOV_XD_DEC : F_FST<0b1011,
  (outs GPR:$Rn_wb), (ins GPR:$Rn, XFPR64:$FRm),
  "fmov\t$FRm, @-$Rn", []> { let Constraints = "$Rn = $Rn_wb"; }

// FMOV XDm,@(R0,Rn)    (1111 nnnn mmmm 0111)
let mayStore = 1, Uses = [R0, FPSCR] in
def FMOV_XD_R0STO : F_FST<0b0111,
  (outs), (ins GPR:$Rn, XFPR64:$FRm),
  "fmov\t$FRm, @(r0, ${Rn})", []>;
} // end let isCodeGenOnly

//===----------------------------------------------------------------------===//
// FP Bank Switch
//===----------------------------------------------------------------------===//

// FRCHG  — flip FPSCR.FR  (1111 0011 1111 1101)
let Defs = [FPSCR] in
def FRCHG : F_Bare<0b1111001111111101, (outs), (ins), "frchg", []>;

// FSCHG  — flip FPSCR.SZ  (1111 0001 1111 1101)
let Defs = [FPSCR] in
def FSCHG : F_Bare<0b1111000111111101, (outs), (ins), "fschg", []>;

//===----------------------------------------------------------------------===//
// Graphics (FIPR, FTRV)
//===----------------------------------------------------------------------===//

// FIPR FVm,FVn  — inner product of 4-element float vectors
// 1111 nn mm 1110 1101  (FVn at bits[11:10], FVm at bits[9:8])
let Uses = [FPSCR] in
def FIPR : F_FIPR<
  (outs FPV32:$FVn), (ins FPV32:$FVm, FPV32:$FVn_src),
  "fipr\t$FVm, $FVn", []> {
  let Constraints = "$FVn_src = $FVn";
}

// FTRV XMTRX,FVn  — 4×4 matrix × 4-vector multiply
// 1111 nn 01 1111 1101  (FVn at bits[11:10])
let Uses = [XMTRX, FPSCR] in
def FTRV : F_FTRV<
  (outs FPV32:$FVn), (ins FPV32:$FVn_src),
  "ftrv\txmtrx, $FVn", []> {
  let Constraints = "$FVn_src = $FVn";
}

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

// Zero/sign-extend loads
def : Pat<(extloadi8   GPR:$p), (EXTU_B (MOVB_IND GPR:$p))>;
def : Pat<(zextloadi8  GPR:$p), (EXTU_B (MOVB_IND GPR:$p))>;
def : Pat<(zextloadi16 GPR:$p), (EXTU_W (MOVW_IND GPR:$p))>;

// Load global/external address from constant pool
def : Pat<(SHwrapper tconstpool:$addr), (MOVL_PCREL tconstpool:$addr)>;

// Atomic fence
def : Pat<(atomic_fence timm, timm), (NOP)>;

// Direct calls removed
def : Pat<(SHread_gbr), (STC_GBR)>;
