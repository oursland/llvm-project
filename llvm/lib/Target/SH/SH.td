//===- SH.td - Describe the SH Target Machine ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// SH Subtarget Features
//===----------------------------------------------------------------------===//

def FeatureSH2 : SubtargetFeature<"sh2", "IsSH2", "true", "Enable SH2 instructions">;
def FeatureSH2A : SubtargetFeature<"sh2a", "IsSH2A", "true", "Enable SH2A instructions">;
def FeatureSH3 : SubtargetFeature<"sh3", "IsSH3", "true", "Enable SH3 instructions">;
def FeatureSH4 : SubtargetFeature<"sh4", "IsSH4", "true", "Enable SH4 instructions">;
def FeatureSH4A : SubtargetFeature<"sh4a", "IsSH4A", "true", "Enable SH4A instructions">;

def FeatureFPU : SubtargetFeature<"fpu", "HasFPU", "true", "Enable floating point instructions">;
def FeatureDPFPU : SubtargetFeature<"dpfpu", "HasDPFPU", "true", "Enable double precision instructions">;

//===----------------------------------------------------------------------===//
// Registers, calling conventions, instruction descriptions
//===----------------------------------------------------------------------===//

include "SHRegisterInfo.td"
include "SHInstrInfo.td"
include "SHCallingConv.td"

def SHInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Processors
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
    : Processor<Name, NoItineraries, Features>;

// Processor list mirrors GCC's -m CPU flags. FPU presence and precision are
// modeled by FeatureFPU (any FPU) and FeatureDPFPU (double-precision FPU).
// The single-only vs. single-default distinction from GCC is collapsed into
// "has DP hardware" for codegen purposes.
def : Proc<"generic",    []>;
def : Proc<"sh1",        []>;
def : Proc<"sh2",        [FeatureSH2]>;
def : Proc<"sh2e",       [FeatureSH2, FeatureFPU]>;
def : Proc<"sh2a",       [FeatureSH2, FeatureSH2A]>;
def : Proc<"sh2a-fpu",   [FeatureSH2, FeatureSH2A, FeatureFPU, FeatureDPFPU]>;
def : Proc<"sh3",        [FeatureSH3]>;
def : Proc<"sh3e",       [FeatureSH3, FeatureFPU]>;
def : Proc<"sh4-nofpu",  [FeatureSH4]>;
def : Proc<"sh4",        [FeatureSH4, FeatureFPU, FeatureDPFPU]>;
def : Proc<"sh4a-nofpu", [FeatureSH4, FeatureSH4A]>;
def : Proc<"sh4a",       [FeatureSH4, FeatureSH4A, FeatureFPU, FeatureDPFPU]>;

//===----------------------------------------------------------------------===//
// Target
//===----------------------------------------------------------------------===//

def SHAsmParser : AsmParser {
  let ShouldEmitMatchRegisterName = 1;
}

def SH : Target {
  let InstructionSet = SHInstrInfo;
  let AssemblyParsers = [SHAsmParser];
}
