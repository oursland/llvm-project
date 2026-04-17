//===- SHRegisterInfo.td - SH Register defs --------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// DWARF register mapping (Linux SH ELF ABI):
//   R0-R15  = 0-15
//   PC      = 16    (not directly accessible)
//   PR      = 17
//   GBR     = 18
//   VBR     = 19
//   MACH    = 20
//   MACL    = 21
//   SR      = 22
//   FPUL    = 23
//   FPSCR   = 24
//   FR0-FR15 = 25-40
//   SSR     = 41
//   SPC     = 42
//   DBR     = 43
//   XF0-XF15 = 77-92
//
//===----------------------------------------------------------------------===//

class SHReg<bits<4> enc, string n, list<Register> subregs = [],
             list<string> altNames = []>
    : Register<n, altNames> {
  let HWEncoding{3-0} = enc;
  let Namespace = "SH";
  let SubRegs = subregs;
}

//===----------------------------------------------------------------------===//
// Sub-register indices
//===----------------------------------------------------------------------===//

let Namespace = "SH" in {
  // Sub-register indices for DR/XD pairs: DRn = (sub_even:FRn, sub_odd:FRn+1)
  // On SH, the even-numbered FR holds the MSW (high 32 bits) and the
  // odd-numbered FR holds the LSW (low 32 bits) of a double.
  def sub_even : SubRegIndex<32>;
  def sub_odd  : SubRegIndex<32, 32>;
}

//===----------------------------------------------------------------------===//
// General-purpose integer registers R0-R15
//===----------------------------------------------------------------------===//

foreach i = 0 - 15 in
  def R#i : SHReg<i, "r"#i>, DwarfRegNum<[i]>;

//===----------------------------------------------------------------------===//
// Integer special-purpose registers
//===----------------------------------------------------------------------===//

def PR   : SHReg<0, "pr">,   DwarfRegNum<[17]>; // Procedure Register (link)
def GBR  : SHReg<0, "gbr">,  DwarfRegNum<[18]>; // Global Base Register
def VBR  : SHReg<0, "vbr">,  DwarfRegNum<[19]>; // Vector Base Register
def MACH : SHReg<0, "mach">, DwarfRegNum<[20]>; // Multiply-Accumulate High
def MACL : SHReg<0, "macl">, DwarfRegNum<[21]>; // Multiply-Accumulate Low
def SR   : SHReg<0, "sr">,   DwarfRegNum<[22]>; // Status Register (T/S bits)

// SH-specific privileged registers (supervisor mode only)
def SSR  : SHReg<0, "ssr">,  DwarfRegNum<[41]>; // Saved Status Register
def SPC  : SHReg<0, "spc">,  DwarfRegNum<[42]>; // Saved Program Counter
def DBR  : SHReg<0, "dbr">,  DwarfRegNum<[43]>; // Debug Base Register

//===----------------------------------------------------------------------===//
// Floating-point system registers
//===----------------------------------------------------------------------===//

def FPUL : SHReg<0, "fpul">,  DwarfRegNum<[23]>; // FP Communication Register
def FPSCR: SHReg<0, "fpscr">, DwarfRegNum<[24]>; // FP Status/Control Register

//===----------------------------------------------------------------------===//
// FP single-precision registers FR0-FR15  (DWARF 25-40)
//===----------------------------------------------------------------------===//

foreach i = 0 - 15 in
  def FR#i : SHReg<i, "fr"#i>, DwarfRegNum<[!add(25, i)]>;

//===----------------------------------------------------------------------===//
// FP double-precision registers DR0,DR2,...,DR14
// Each DRn represents the pair (FRn, FRn+1), n must be even.
// HW encoding = n (the even FR index).
// Sub-register: sub_even=FRn (MSW), sub_odd=FRn+1 (LSW).
//===----------------------------------------------------------------------===//

let SubRegIndices = [sub_even, sub_odd], CoveredBySubRegs = 1 in {
def DR0  : SHReg<0,  "dr0",  [FR0,  FR1]>,  DwarfRegNum<[25]>;
def DR2  : SHReg<2,  "dr2",  [FR2,  FR3]>,  DwarfRegNum<[27]>;
def DR4  : SHReg<4,  "dr4",  [FR4,  FR5]>,  DwarfRegNum<[29]>;
def DR6  : SHReg<6,  "dr6",  [FR6,  FR7]>,  DwarfRegNum<[31]>;
def DR8  : SHReg<8,  "dr8",  [FR8,  FR9]>,  DwarfRegNum<[33]>;
def DR10 : SHReg<10, "dr10", [FR10, FR11]>, DwarfRegNum<[35]>;
def DR12 : SHReg<12, "dr12", [FR12, FR13]>, DwarfRegNum<[37]>;
def DR14 : SHReg<14, "dr14", [FR14, FR15]>, DwarfRegNum<[39]>;
}

//===----------------------------------------------------------------------===//
// Shadow/extended FP registers XF0-XF15  (DWARF 77-92)
// Accessible only when FPSCR.FR=1.
//===----------------------------------------------------------------------===//

foreach i = 0 - 15 in
  def XF#i : SHReg<i, "xf"#i>, DwarfRegNum<[!add(77, i)]>;

//===----------------------------------------------------------------------===//
// Shadow FP double-precision registers XD0,XD2,...,XD14
// Each XDn represents the pair (XFn, XFn+1), n must be even.
//===----------------------------------------------------------------------===//

// XD register encoding: odd values (DRn uses even n, XDn uses n+1 to distinguish).
// XD0→1, XD2→3, XD4→5, XD6→7, XD8→9, XD10→11, XD12→13, XD14→15.
let SubRegIndices = [sub_even, sub_odd], CoveredBySubRegs = 1 in {
def XD0  : SHReg<1,  "xd0",  [XF0,  XF1]>,  DwarfRegNum<[77]>;
def XD2  : SHReg<3,  "xd2",  [XF2,  XF3]>,  DwarfRegNum<[79]>;
def XD4  : SHReg<5,  "xd4",  [XF4,  XF5]>,  DwarfRegNum<[81]>;
def XD6  : SHReg<7,  "xd6",  [XF6,  XF7]>,  DwarfRegNum<[83]>;
def XD8  : SHReg<9,  "xd8",  [XF8,  XF9]>,  DwarfRegNum<[85]>;
def XD10 : SHReg<11, "xd10", [XF10, XF11]>, DwarfRegNum<[87]>;
def XD12 : SHReg<13, "xd12", [XF12, XF13]>, DwarfRegNum<[89]>;
def XD14 : SHReg<15, "xd14", [XF14, XF15]>, DwarfRegNum<[91]>;
}

//===----------------------------------------------------------------------===//
// FP 4-element vector registers FV0, FV4, FV8, FV12
// FVn groups FRn through FRn+3.  Used by FIPR and FTRV.
// HW encoding in instructions: FV0=0b00, FV4=0b01, FV8=0b10, FV12=0b11
// (i.e. first_FR_index / 4).
//===----------------------------------------------------------------------===//

def FV0  : SHReg<0, "fv0">,  DwarfRegNum<[25]>;
def FV4  : SHReg<1, "fv4">,  DwarfRegNum<[29]>;
def FV8  : SHReg<2, "fv8">,  DwarfRegNum<[33]>;
def FV12 : SHReg<3, "fv12">, DwarfRegNum<[37]>;

// Extended matrix register: the full 4×4 matrix formed by XF0-XF15.
// Implicit operand of FTRV.
def XMTRX : SHReg<0, "xmtrx">, DwarfRegNum<[77]>;

//===----------------------------------------------------------------------===//
// Register Classes
//===----------------------------------------------------------------------===//

// General purpose — caller-saved first (R0-R7), callee-saved next (R8-R14),
// stack pointer (R15) last to discourage allocation.
def GPR : RegisterClass<"SH", [i32], 32,
  (add R0, R1, R2, R3, R4, R5, R6, R7,
       R8, R9, R10, R11, R12, R13, R14, R15)>;

// GPR without R0 — used for scratch registers in eliminateFrameIndex.
// R0 is special on SH: it's the only register that can be used for
// indexed addressing (@(r0,rn)) and is implicitly used by many instructions.
// If the register allocator assigns R0 for a long-lived value, and
// eliminateFrameIndex resolves a scratch virtual register to R0, the
// previously-allocated R0 value gets clobbered.
def GPR_No_R0 : RegisterClass<"SH", [i32], 32,
  (add R1, R2, R3, R4, R5, R6, R7,
       R8, R9, R10, R11, R12, R13, R14, R15)>;

// Status register (T/S bits)
def FPSR : RegisterClass<"SH", [i32], 32, (add SR)>;

// MAC register pair
def MACREG : RegisterClass<"SH", [i32], 32, (add MACH, MACL)>;

// FP single-precision (main bank)
def FPR32 : RegisterClass<"SH", [f32], 32,
  (add FR0, FR1, FR2, FR3, FR4, FR5, FR6, FR7,
       FR8, FR9, FR10, FR11, FR12, FR13, FR14, FR15)>;

// FP double-precision (main bank, even registers only)
// Alignment is 32 bits (4 bytes) because SH stores doubles as two fmov.s
// halves, each 4-byte aligned.  alignof(double) == 4 in the SH ABI.
def FPR64 : RegisterClass<"SH", [f64], 32,
  (add DR0, DR2, DR4, DR6, DR8, DR10, DR12, DR14)>;

// Shadow/extended FP single-precision (second bank, FPSCR.FR=1)
def XFPR32 : RegisterClass<"SH", [f32], 32,
  (add XF0, XF1, XF2, XF3, XF4, XF5, XF6, XF7,
       XF8, XF9, XF10, XF11, XF12, XF13, XF14, XF15)>;

// Shadow FP double-precision (second bank)
def XFPR64 : RegisterClass<"SH", [f64], 32,
  (add XD0, XD2, XD4, XD6, XD8, XD10, XD12, XD14)>;

// FP 4-element float vector (used by FIPR, FTRV)
def FPV32 : RegisterClass<"SH", [v4f32], 128,
  (add FV0, FV4, FV8, FV12)>;

// FP communication register
def FPULREG : RegisterClass<"SH", [i32], 32, (add FPUL)>;

// FP status/control register
def FPSCREG : RegisterClass<"SH", [i32], 32, (add FPSCR)>;
