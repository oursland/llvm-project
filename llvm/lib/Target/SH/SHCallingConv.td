//===- SHCallingConv.td - Calling Conventions for SH -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for the SH Linux ABI.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Return Value Calling Convention
//===----------------------------------------------------------------------===//
def RetCC_SH : CallingConv<[
  // f32 returned in FR0 if FPU is present
  CCIfType<[f32], CCIf<"State.getMachineFunction().getSubtarget<SHSubtarget>().hasFPU()", CCAssignToReg<[FR0]>>>,
  // f64 returned in DR0 if FPU is present
  CCIfType<[f64], CCIf<"State.getMachineFunction().getSubtarget<SHSubtarget>().hasFPU()", CCAssignToReg<[DR0]>>>,
  // When no FPU is present, bitcast f32 to i32 and f64 to two i32s (or i64). LLVM core usually handles struct returns, but we can coerce f32 to i32.
  CCIfType<[f32], CCIf<"!State.getMachineFunction().getSubtarget<SHSubtarget>().hasFPU()", CCBitConvertToType<i32>>>,
  CCIfType<[f64], CCIf<"!State.getMachineFunction().getSubtarget<SHSubtarget>().hasFPU()", CCBitConvertToType<i64>>>,
  CCIfType<[i64], CCIf<"!State.getMachineFunction().getSubtarget<SHSubtarget>().hasFPU()", CCBitConvertToType<i32>>>, // Actually i64 isn't supported without bitcast in CC?

  // i32 returned in R0 (and R1 for 64-bit values).
  CCIfType<[i32], CCAssignToReg<[R0, R1]>>
]>;

//===----------------------------------------------------------------------===//
// Argument Calling Convention
//===----------------------------------------------------------------------===//
def CC_SH : CallingConv<[
  // Handle byval arguments — allocate on the stack.
  CCIfByVal<CCPassByVal<4, 4>>,
  // Promote i8/i16 to i32.
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  // f32 arguments go in pairs (FR5, FR4), (FR7, FR6), etc. in little endian mode, if FPU enabled.
  CCIfType<[f32], CCIf<"State.getMachineFunction().getSubtarget<SHSubtarget>().hasFPU()", CCAssignToReg<[FR5, FR4, FR7, FR6, FR9, FR8, FR11, FR10]>>>,
  // f64 arguments go in DR4, DR6, DR8, DR10, if FPU enabled.
  CCIfType<[f64], CCIf<"State.getMachineFunction().getSubtarget<SHSubtarget>().hasFPU()", CCAssignToReg<[DR4, DR6, DR8, DR10]>>>,

  // SoftFloat: Bitconvert floats to integers.
  CCIfType<[f32], CCIf<"!State.getMachineFunction().getSubtarget<SHSubtarget>().hasFPU()", CCBitConvertToType<i32>>>,
  CCIfType<[f64], CCIf<"!State.getMachineFunction().getSubtarget<SHSubtarget>().hasFPU()", CCBitConvertToType<i64>>>,
  // Note: custom ISel lowering splits i64 into two i32s naturally, so we don't handle i64 here directly because it is custom lower returned.
  // First four i32 arguments go in R4-R7.
  CCIfType<[i32], CCAssignToReg<[R4, R5, R6, R7]>>,
  // Remaining arguments on stack, 4-byte aligned.
  CCIfType<[i32, f32], CCAssignToStack<4, 4>>,
  CCIfType<[f64], CCAssignToStack<8, 4>>
]>;

//===----------------------------------------------------------------------===//
// Callee-Saved Registers
//===----------------------------------------------------------------------===//
// R8-R14 are callee-saved. R15 is the stack pointer (implicitly preserved).
// FR12-FR15 are callee-saved floating-point registers per the SH ABI.
// PR (link register) is also saved when making calls.
def CSR_SH : CalleeSavedRegs<(add R8, R9, R10, R11, R12, R13, R14,
                                    FR12, FR13, FR14, FR15)>;
