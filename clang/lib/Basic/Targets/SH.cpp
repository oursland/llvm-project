//===--- SH.cpp - Implement SH target feature support -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements SH TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "SH.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

void SHTargetInfo::getTargetDefines(const LangOptions &Opts,
                                    MacroBuilder &Builder) const {
  // Target family macros.
  Builder.defineMacro("__sh__");
  Builder.defineMacro("__SH__");

  // Determine effective CPU: prefer -mcpu over triple arch.
  StringRef CPU = getTargetOpts().CPU;

  // CPU names match GCC's -m flags (see gcc.gnu.org/onlinedocs SH-Options).
  // -nofpu variants of sh4/sh4a are the FPU-less hardware configurations;
  // sh2a-fpu is the FPU-equipped SH-2A part. `sh` / empty CPU is SH-1.
  enum SHVariant {
    SH1,
    SH2,
    SH2E,
    SH2A,
    SH2A_FPU,
    SH3,
    SH3E,
    SH4_NOFPU,
    SH4,
    SH4A_NOFPU,
    SH4A,
  };
  SHVariant Variant = SH1;

  if (!CPU.empty()) {
    Variant = llvm::StringSwitch<SHVariant>(CPU)
                  .Case("sh1", SH1)
                  .Case("sh2", SH2)
                  .Case("sh2e", SH2E)
                  .Case("sh2a", SH2A)
                  .Case("sh2a-fpu", SH2A_FPU)
                  .Case("sh3", SH3)
                  .Case("sh3e", SH3E)
                  .Case("sh4-nofpu", SH4_NOFPU)
                  .Case("sh4", SH4)
                  .Case("sh4a-nofpu", SH4A_NOFPU)
                  .Case("sh4a", SH4A)
                  .Default(SH1);
  } else {
    switch (getTriple().getArch()) {
    case llvm::Triple::sh2:
      Variant = SH2;
      break;
    case llvm::Triple::sh2a:
      Variant = SH2A;
      break;
    case llvm::Triple::sh3:
      Variant = SH3;
      break;
    case llvm::Triple::sh3e:
      Variant = SH3E;
      break;
    case llvm::Triple::sh4:
      Variant = SH4;
      break;
    case llvm::Triple::sh4a:
      Variant = SH4A;
      break;
    case llvm::Triple::sh:
    default:
      Variant = SH1;
      break;
    }
  }

  // Macro names match GCC's SH backend exactly (see gcc/config/sh/sh-c.cc).
  // Lowercase forms (__shN__) are emitted only for SH1/SH2/SH3 — GCC does not
  // emit them for the 2E/2A/3E/4/4A suffix variants.
  switch (Variant) {
  case SH1:
    Builder.defineMacro("__SH1__", "1");
    Builder.defineMacro("__sh1__");
    break;
  case SH2:
    Builder.defineMacro("__SH2__", "1");
    Builder.defineMacro("__sh2__");
    break;
  case SH2E:
    Builder.defineMacro("__SH2E__", "1");
    Builder.defineMacro("__SH2__", "1");
    Builder.defineMacro("__sh2__");
    Builder.defineMacro("__SH_FPU_ANY__", "1");
    break;
  case SH2A:
    Builder.defineMacro("__SH2A__", "1");
    Builder.defineMacro("__SH2A_NOFPU__", "1");
    break;
  case SH2A_FPU:
    Builder.defineMacro("__SH2A__", "1");
    Builder.defineMacro("__SH2A_DOUBLE__", "1");
    Builder.defineMacro("__SH_FPU_ANY__", "1");
    Builder.defineMacro("__SH_FPU_DOUBLE__", "1");
    break;
  case SH3:
    Builder.defineMacro("__SH3__", "1");
    Builder.defineMacro("__sh3__");
    break;
  case SH3E:
    Builder.defineMacro("__SH3E__", "1");
    Builder.defineMacro("__SH3__", "1");
    Builder.defineMacro("__sh3__");
    Builder.defineMacro("__SH_FPU_ANY__", "1");
    break;
  case SH4_NOFPU:
    Builder.defineMacro("__SH4__", "1");
    Builder.defineMacro("__SH4_NOFPU__", "1");
    break;
  case SH4:
    Builder.defineMacro("__SH4__", "1");
    Builder.defineMacro("__SH_FPU_ANY__", "1");
    Builder.defineMacro("__SH_FPU_DOUBLE__", "1");
    break;
  case SH4A_NOFPU:
    Builder.defineMacro("__SH4A__", "1");
    Builder.defineMacro("__SH4__", "1");
    Builder.defineMacro("__SH4_NOFPU__", "1");
    break;
  case SH4A:
    Builder.defineMacro("__SH4A__", "1");
    Builder.defineMacro("__SH4__", "1");
    Builder.defineMacro("__SH_FPU_ANY__", "1");
    Builder.defineMacro("__SH_FPU_DOUBLE__", "1");
    break;
  }

  // Endianness (SH Linux is little-endian).
  Builder.defineMacro("__LITTLE_ENDIAN__");
  Builder.defineMacro("_LITTLE_ENDIAN");

  // Vendor compatibility macros (GCC defines these).
  Builder.defineMacro("__HITACHI__");
  Builder.defineMacro("__RENESAS__");
  Builder.defineMacro("__RENESAS_VERSION__", "0");
}
