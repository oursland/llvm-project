//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH assembly backend.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SHFixupKinds.h"
#include "MCTargetDesc/SHMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

class SHAsmBackend : public MCAsmBackend {
public:
  SHAsmBackend(const MCSubtargetInfo &STI)
      : MCAsmBackend(llvm::endianness::little) {}
  ~SHAsmBackend() override = default;

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override {
    return std::nullopt;
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    const static MCFixupKindInfo Infos[SH::NumTargetFixupKinds] = {
        // name, offset, bits, flags
        {"fixup_sh_pcrel12", 0, 12, 0},
        {"fixup_sh_pcrel8", 0, 8, 0},
        {"fixup_sh_32", 0, 32, 0},
        {"fixup_sh_pcrel_m8", 0, 8, 0}};

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);

    assert(unsigned(Kind - FirstTargetFixupKind) < SH::NumTargetFixupKinds &&
           "Invalid kind!");
    return Infos[Kind - FirstTargetFixupKind];
  }

  bool fixupNeedsRelaxationAdvanced(const MCFragment &F, const MCFixup &Fixup,
                                    const MCValue &Value,
                                    uint64_t ResolvedValue,
                                    bool Resolved) const override {
    return false;
  }

  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override {}

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {

    // SH nop instruction is 0x0009
    if (Count % 2 != 0) {
      OS.write("\x00", 1);
      Count -= 1;
    }
    for (uint64_t i = 0; i < Count; i += 2) {
      OS.write("\x09\x00", 2);
    }
    return true;
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    maybeAddReloc(F, Fixup, Target, Value, IsResolved);
    if (!Value)
      return; // Doesn't change encoding

    MCFixupKind Kind = Fixup.getKind();
    unsigned Offset = Fixup.getOffset();

    // The SH architecture uses PC-relative addressing heavily.
    // The branch offsets are measured in instructions (words, 2 bytes each)
    // from the *next* instruction (PC + 4 due to the pipeline nature, but in
    // llvm terms we typically handle the PC+4 in the Fixup value calculation).
    switch ((unsigned)Kind) {
    default:
      llvm_unreachable("Unknown fixup kind!");
    case FK_Data_4:
    case SH::fixup_sh_32:
      support::endian::write32le(Data, Value);
      break;
    case SH::fixup_sh_pcrel12: {
      int64_t SValue = ((int64_t)Value - 4) / 2;
      if (!isInt<12>(SValue))
        report_fatal_error(
            "SH BRA/BSR displacement overflow: Disp=" + Twine(SValue) +
            " at offset 0x" +
            Twine::utohexstr(Asm->getFragmentOffset(F) + Offset));
      uint16_t CurData = support::endian::read16le(Data);
      CurData |= (uint16_t)SValue & 0xFFF;
      support::endian::write16le(Data, CurData);
      break;
    }
    case SH::fixup_sh_pcrel8: {
      int64_t SValue = ((int64_t)Value - 4) / 2;
      if (!isInt<8>(SValue))
        report_fatal_error(
            "SH BT/BF displacement overflow: Disp=" + Twine(SValue) +
            " Value=" + Twine((int64_t)Value) + " at offset 0x" +
            Twine::utohexstr(Asm->getFragmentOffset(F) + Offset));
      uint16_t CurData = support::endian::read16le(Data);
      CurData |= (uint16_t)SValue & 0xFF;
      support::endian::write16le(Data, CurData);
      break;
    }
    case SH::fixup_sh_pcrel_m8: {
      // The offset in the fragment is still needed for alignment
      uint32_t FixupOffsetInSection = Asm->getFragmentOffset(F) + Offset;
      uint32_t AlignDiff = 4 - (FixupOffsetInSection % 4);
      int64_t Disp = (int64_t)(Value - AlignDiff) / 4;
      if (!isUInt<8>(Disp))
        report_fatal_error(
            "SH mov.l PC-relative displacement overflow: Disp=" + Twine(Disp) +
            " Value=" + Twine((int64_t)Value) + " at offset 0x" +
            Twine::utohexstr(FixupOffsetInSection));
      uint16_t CurData = support::endian::read16le(Data);
      CurData |= Disp & 0xFF;
      support::endian::write16le(Data, CurData);
      break;
    }
    }
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createSHELFObjectWriter(0);
  }
};

} // end anonymous namespace

MCAsmBackend *llvm::createSHAsmBackend(const Target &T,
                                       const MCSubtargetInfo &STI,
                                       const MCRegisterInfo &MRI,
                                       const MCTargetOptions &Options) {
  return new SHAsmBackend(STI);
}
