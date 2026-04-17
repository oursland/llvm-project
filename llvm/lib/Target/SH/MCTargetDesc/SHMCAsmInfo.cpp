//===- SHMCAsmInfo.cpp - SH asm properties --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements SH-specific MC assembly info.
//
//===----------------------------------------------------------------------===//

#include "SHMCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

static const MCAsmInfo::AtSpecifier atSpecifiers[] = {{SH::S_GOT, "GOT"},
                                                      {SH::S_GOTPC, "GOTPC"},
                                                      {SH::S_PLT, "PLT"},
                                                      {SH::S_TPOFF, "TPOFF"}};

void SHMCAsmInfo::anchor() {}

SHMCAsmInfo::SHMCAsmInfo(const Triple &TT) {
  IsLittleEndian = true;
  SupportsDebugInformation = true;
  MinInstAlignment = 2;
  CommentString = "!";
  Data8bitsDirective = "\t.byte\t";
  Data16bitsDirective = "\t.short\t";
  Data32bitsDirective = "\t.long\t";
  Data64bitsDirective = "\t.quad\t";
  ZeroDirective = "\t.zero\t";
  UsesELFSectionDirectiveForBSS = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;

  initializeAtSpecifiers(atSpecifiers);
}
