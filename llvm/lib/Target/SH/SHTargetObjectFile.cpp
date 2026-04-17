//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements SH-specific ELF object file handling.
//
//===----------------------------------------------------------------------===//

#include "SHTargetObjectFile.h"
#include "llvm/IR/Constant.h"

using namespace llvm;

MCSection *SHTargetObjectFile::getSectionForConstant(const DataLayout &DL,
                                                     SectionKind Kind,
                                                     const Constant *C,
                                                     Align &Alignment,
                                                     const Function *F) const {
  // Let the ELF object file class handle standard sections like .rodata or
  // .debug*
  return TargetLoweringObjectFileELF::getSectionForConstant(DL, Kind, C,
                                                            Alignment, F);
}
