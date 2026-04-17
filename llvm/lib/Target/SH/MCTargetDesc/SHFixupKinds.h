//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SH_MCTARGETDESC_SHFIXUPKINDS_H
#define LLVM_LIB_TARGET_SH_MCTARGETDESC_SHFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace SH {

// This table *must* be in the same order of
// MCFixupKindInfo::FixupKindInfo infos in SHAsmBackend.cpp.
//
// Note: SH instructions are 16 bits.
enum Fixups {
  // 12-bit PC-relative branch target (e.g. bra, bsr)
  fixup_sh_pcrel12 = FirstTargetFixupKind,

  // 8-bit PC-relative branch target (e.g. bt, bf)
  fixup_sh_pcrel8,

  // 32-bit absolute address
  fixup_sh_32,

  // 8-bit PC-relative branch target for memory loads (e.g. mov.l @(disp,pc))
  fixup_sh_pcrel_m8,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // namespace SH
} // namespace llvm

#endif // LLVM_LIB_TARGET_SH_MCTARGETDESC_SHFIXUPKINDS_H
