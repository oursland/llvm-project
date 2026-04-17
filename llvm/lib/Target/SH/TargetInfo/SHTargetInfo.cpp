//===- SHTargetInfo.cpp - SH Target Implementation ----------- *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file registers the SH target.
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/SHTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheSHTarget() {
  static Target TheSHTarget;
  return TheSHTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSHTargetInfo() {
  // Register the target to match all SH sub-architecture triples.
  TargetRegistry::RegisterTarget(
      getTheSHTarget(), "sh", "SuperH SH (little-endian)", "SH",
      [](Triple::ArchType Arch) {
        return Arch == Triple::sh || Arch == Triple::sh2 ||
               Arch == Triple::sh2a || Arch == Triple::sh3 ||
               Arch == Triple::sh3e || Arch == Triple::sh4 ||
               Arch == Triple::sh4a;
      },
      /*HasJIT=*/false);
}
