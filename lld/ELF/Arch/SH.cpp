//===- SH.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {

class SH final : public TargetInfo {
public:
  SH(Ctx &ctx);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
  void writeGotPltHeader(uint8_t *buf) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  RelType getDynRel(RelType type) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
};

} // namespace

SH::SH(Ctx &ctx) : TargetInfo(ctx) {
  defaultMaxPageSize = 65536;
  defaultCommonPageSize = 4096;
  symbolicRel = R_SH_DIR32;
  relativeRel = R_SH_RELATIVE;
  gotRel = R_SH_GLOB_DAT;
  pltRel = R_SH_JMP_SLOT;
  copyRel = R_SH_COPY;
  tlsModuleIndexRel = R_SH_TLS_DTPMOD32;
  tlsOffsetRel = R_SH_TLS_DTPOFF32;
  tlsGotRel = R_SH_TLS_TPOFF32;

  // PLT0 (header): 28 bytes, each PLT entry: 28 bytes
  pltHeaderSize = 28;
  pltEntrySize = 28;
  ipltEntrySize = 28;

  // GOT header: GOT[0] = _DYNAMIC, GOT[1] = link_map, GOT[2] = resolver
  gotHeaderEntriesNum = 3;
}

RelExpr SH::getRelExpr(RelType type, const Symbol &s,
                       const uint8_t *loc) const {
  switch (type) {
  case R_SH_DIR32:
    return R_ABS;
  case R_SH_REL32:
  case R_SH_IND12W:
  case R_SH_DIR8WPN:
  case R_SH_DIR8WPZ:
  case R_SH_REL12:
    return R_PC;
  case R_SH_GOT32:
    return R_GOT_OFF;
  case R_SH_PLT32:
    return R_PLT_PC;
  case R_SH_GOTOFF:
    return R_GOTREL;
  case R_SH_GOTPC:
    return R_GOTONLY_PC;
  case R_SH_TLS_IE_32:
    return R_GOT_OFF;
  case R_SH_TLS_LE_32:
    return R_TPREL;
  case R_SH_TLS_DTPMOD32:
  case R_SH_TLS_DTPOFF32:
  case R_SH_TLS_TPOFF32:
    return R_ABS;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unknown relocation (" << type
             << ") against symbol " << &s;
    return R_NONE;
  }
}

int64_t SH::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  case R_SH_DIR32:
  case R_SH_REL32:
  case R_SH_GOT32:
  case R_SH_PLT32:
  case R_SH_GOTOFF:
  case R_SH_GOTPC:
  case R_SH_GLOB_DAT:
  case R_SH_JMP_SLOT:
  case R_SH_RELATIVE:
  case R_SH_COPY:
  case R_SH_TLS_IE_32:
  case R_SH_TLS_LE_32:
  case R_SH_TLS_DTPMOD32:
  case R_SH_TLS_DTPOFF32:
  case R_SH_TLS_TPOFF32:
    return SignExtend64<32>(read32le(buf));
  case R_SH_DIR8WPN:
  case R_SH_DIR8WPZ:
    return SignExtend64<8>(*buf);
  case R_SH_IND12W:
  case R_SH_REL12: {
    uint16_t val = read16le(buf);
    return SignExtend64<12>((val & 0x0fff) << 1);
  }
  case R_SH_NONE:
    return 0;
  default:
    InternalErr(ctx, buf) << "cannot read addend for relocation " << type;
    return 0;
  }
}

void SH::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_SH_DIR32:
  case R_SH_GOT32:
  case R_SH_GOTOFF:
  case R_SH_GOTPC:
    write32le(loc, val);
    break;
  case R_SH_REL32:
  case R_SH_PLT32:
    // SH RELA PC-relative relocations: add existing section data (implicit
    // addend) to the computed S+A-P. The assembler stores a pre-computed
    // offset (e.g., distance from bsrf+4 to the literal pool) in the section
    // data, and the linker adds the relocation result to it.
    write32le(loc, val + read32le(loc));
    break;
  case R_SH_DIR8WPN:
  case R_SH_DIR8WPZ: {
    // 8-bit PC-relative forward branch, disp * 2
    checkAlignment(ctx, loc, val, 2, rel);
    int64_t d = val - 4;
    checkInt(ctx, loc, d, 9, rel);
    loc[0] = (loc[0] & 0x00) | ((d >> 1) & 0xff);
    break;
  }
  case R_SH_IND12W:
  case R_SH_REL12: {
    // 12-bit PC-relative branch, disp * 2
    checkAlignment(ctx, loc, val, 2, rel);
    int64_t d = val - 4;
    checkInt(ctx, loc, d, 13, rel);
    loc[0] = d >> 1;
    loc[1] = (loc[1] & 0xf0) | ((d >> 9) & 0x0f);
    break;
  }
  case R_SH_TLS_IE_32:
  case R_SH_TLS_LE_32:
  case R_SH_TLS_DTPMOD32:
  case R_SH_TLS_DTPOFF32:
  case R_SH_TLS_TPOFF32:
    write32le(loc, val);
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

// GOT.PLT[0] = address of _DYNAMIC section
void SH::writeGotPltHeader(uint8_t *buf) const {
  write32le(buf, ctx.mainPart->dynamic->getVA());
}

// Initialize each GOT.PLT entry to point to the lazy binding code
// in the corresponding PLT entry. PLTn+8 is the 'mov r1, r0' instruction
// that sets r0 = PLT0 addr, then falls through to load reloc offset
// and jump to PLT0.
void SH::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  write32le(buf, s.getPltVA(ctx) + 8);
}

// SH PLT header (PLT0) - 28 bytes
// Called by the dynamic linker to resolve symbols on first call.
//
// SH PLT0 format (little-endian):
//   mov.l  @(20,pc), r0    ! load &GOT[1] (link_map ptr)
//   mov.l  @r0, r0         ! r0 = link_map
//   mov.l  r0, @-r15       ! push link_map onto stack
//   mov.l  @(12,pc), r0    ! load &GOT[2] (resolver ptr)
//   mov.l  @r0, r0         ! r0 = resolver function
//   jmp    @r0             ! jump to resolver
//   mov.l  @r15+, r0       ! (delay slot) pop into r0 for resolver
//   nop
//   nop
//   nop
//   .long  &GOT[1]         ! absolute address of GOT[1]
//   .long  &GOT[2]         ! absolute address of GOT[2]
void SH::writePltHeader(uint8_t *buf) const {
  // Instructions (little-endian 16-bit)
  write16le(buf + 0, 0xd005);  // mov.l @(20, pc), r0
  write16le(buf + 2, 0x6002);  // mov.l @r0, r0
  write16le(buf + 4, 0x2f06);  // mov.l r0, @-r15
  write16le(buf + 6, 0xd003);  // mov.l @(12, pc), r0
  write16le(buf + 8, 0x6002);  // mov.l @r0, r0
  write16le(buf + 10, 0x402b); // jmp @r0
  write16le(buf + 12, 0x60f6); // mov.l @r15+, r0  (delay slot)
  write16le(buf + 14, 0x0009); // nop
  write16le(buf + 16, 0x0009); // nop
  write16le(buf + 18, 0x0009); // nop
  // Literal pool:
  //   buf+20: loaded by mov.l @(3,pc) at buf+6  → &GOT[2] (resolver)
  //   buf+24: loaded by mov.l @(5,pc) at buf+0  → &GOT[1] (link_map)
  uint64_t gotAddr = ctx.in.gotPlt->getVA();
  write32le(buf + 20, gotAddr + 8); // &GOT[2] = resolver
  write32le(buf + 24, gotAddr + 4); // &GOT[1] = link_map
}

// SH PLT entry (PLTn) - 28 bytes
//
// Each PLT entry resolves one function. On first call, jumps to PLT0
// (resolver) with the relocation offset. After resolution, jumps
// directly to the function.
//
// Format:
//   mov.l  @(16,pc), r0    ! load &GOT[n] (function's GOT entry addr)
//   mov.l  @r0, r0         ! r0 = function address (or PLT fallthru)
//   mov.l  @(16,pc), r0    ! load &GOT[n] (loads from buf+20)
//   mov.l  @r0, r0         ! r0 = function address (or PLT fallthru)
//   mov.l  @(8,pc), r1     ! load PLT0 address (loads from buf+16)
//   jmp    @r0             ! jump to function
//   mov    r1, r0          ! (delay slot) r0 = PLT0 addr
//   mov.l  @(12,pc), r1   ! load relocation offset (loads from buf+24)
//   jmp    @r0             ! jump to PLT0
//   nop                    ! (delay slot)
//   .long  PLT0_addr       ! [buf+16] loaded by mov.l @(8,pc) at buf+4
//   .long  &GOT[n]         ! [buf+20] loaded by mov.l @(16,pc) at buf+0
//   .long  reloc_offset    ! [buf+24] loaded by mov.l @(12,pc) at buf+10
void SH::writePlt(uint8_t *buf, const Symbol &sym,
                  uint64_t pltEntryAddr) const {
  // Instructions
  write16le(buf + 0, 0xd004);  // mov.l @(16, pc), r0  -> loads buf+20
  write16le(buf + 2, 0x6002);  // mov.l @r0, r0
  write16le(buf + 4, 0xd102);  // mov.l @(8, pc), r1   -> loads buf+16
  write16le(buf + 6, 0x402b);  // jmp @r0
  write16le(buf + 8, 0x6013);  // mov r1, r0  (delay slot)
  write16le(buf + 10, 0xd103); // mov.l @(12, pc), r1  -> loads buf+24
  write16le(buf + 12, 0x402b); // jmp @r0
  write16le(buf + 14, 0x0009); // nop (delay slot)

  // Literal pool
  uint64_t plt0Addr = ctx.in.plt->getVA();
  uint64_t gotPltEntryAddr = sym.getGotPltVA(ctx);
  uint32_t relocOff = ctx.in.relaPlt->entsize * sym.getPltIdx(ctx);

  write32le(buf + 16, plt0Addr);        // [buf+16] PLT0 address
  write32le(buf + 20, gotPltEntryAddr); // [buf+20] GOT entry for this function
  write32le(buf + 24, relocOff);        // [buf+24] relocation offset
}

RelType SH::getDynRel(RelType type) const {
  return type == ctx.target->symbolicRel ? type : R_SH_NONE;
}

void elf::setSHTargetInfo(Ctx &ctx) { ctx.target.reset(new SH(ctx)); }
