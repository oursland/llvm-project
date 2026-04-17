//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH ELF object writer.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SHFixupKinds.h"
#include "MCTargetDesc/SHMCAsmInfo.h"
#include "MCTargetDesc/SHMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class SHELFObjectWriter : public MCELFObjectTargetWriter {
public:
  SHELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit=*/false, OSABI, ELF::EM_SH,
                                /*HasRelocationAddend=*/true) {}

  ~SHELFObjectWriter() override = default;

  /// GNU ld for SH reads the relocation addend from instruction data,
  /// not the RELA addend field.  GAS matches: .rela sections with
  /// r_addend=0 and the actual offset embedded in the data.
  bool addendInData() const override { return true; }

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override {
    // Determine the type of the relocation
    switch ((unsigned)Fixup.getKind()) {
    default:
      llvm_unreachable("invalid fixup kind!");
      return ELF::R_SH_NONE;
    case FK_Data_1:
      return IsPCRel ? ELF::R_SH_NONE : ELF::R_SH_DIR8;
    case FK_Data_2:
      return IsPCRel ? ELF::R_SH_NONE : ELF::R_SH_DIR16;
    case FK_Data_4:
    case SH::fixup_sh_32:
      if (IsPCRel)
        return ELF::R_SH_REL32;
      switch (Target.getSpecifier()) {
      case SH::S_GOTPC:
        return ELF::R_SH_GOTPC;
      case SH::S_GOT:
        return ELF::R_SH_GOT32;
      case SH::S_PLT:
        return ELF::R_SH_PLT32;
      case SH::S_TPOFF:
        return ELF::R_SH_TLS_LE_32;
      default:
        return ELF::R_SH_DIR32;
      }
    case FK_Data_8:
      return IsPCRel ? ELF::R_SH_NONE
                     : ELF::R_SH_DIR32; // SH doesn't have 64-bit relocs, DWARF
                                        // will just fallback to DIR32 or emit
                                        // two 32s.
    case FK_SecRel_2:
      return ELF::R_SH_DIR16;
    case FK_SecRel_4:
      return ELF::R_SH_DIR32;
    case SH::fixup_sh_pcrel12:
      return ELF::R_SH_IND12W;
    case SH::fixup_sh_pcrel8:
      return ELF::R_SH_DIR8WPN;
    }
  }
};
} // end anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createSHELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<SHELFObjectWriter>(OSABI);
}
