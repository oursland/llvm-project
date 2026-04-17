//===- SHMCAsmInfo.h - SH asm properties ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SH_MCTARGETDESC_SHMCASMINFO_H
#define LLVM_LIB_TARGET_SH_MCTARGETDESC_SHMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"
#include "llvm/MC/MCExpr.h"

namespace llvm {

class Triple;

class SHMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit SHMCAsmInfo(const Triple &TT);
};

} // end namespace llvm

namespace llvm {
namespace SH {
using Specifier = uint16_t;
enum {
  S_None,
  S_SH_NONE = MCSymbolRefExpr::FirstTargetSpecifier,
  S_GOT,
  S_GOTPC,
  S_PLT,
  S_TPOFF
};
} // end namespace SH
} // end namespace llvm

#endif // LLVM_LIB_TARGET_SH_MCTARGETDESC_SHMCASMINFO_H
