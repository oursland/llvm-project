//===--- SH.h - Declare SH target feature support ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares SH TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_SH_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_SH_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY SHTargetInfo : public TargetInfo {
public:
  SHTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    NoAsmVariants = true;
    LongLongAlign = 32;
    SuitableAlign = 32;
    DefaultAlignForAttributeAligned = 32;
    DoubleAlign = 32;
    LongDoubleWidth = 64;
    LongDoubleAlign = 32;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    MinGlobalAlign = 32;

    SizeType = UnsignedInt;
    PtrDiffType = SignedInt;
    IntPtrType = SignedInt;

    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 32;

    resetDataLayout("e-m:e-p:32:32-i8:8:32-i16:16:32-f64:32:32-i64:32:32-n32");
  }

  // SH does not guarantee 8-byte stack alignment for doubles.
  // Prevent Clang from bumping double preferred alignment to sizeof(double).
  bool allowsLargerPreferedTypeAlignment() const override { return false; }

  bool hasBitIntType() const override { return true; }

  bool isValidCPUName(StringRef Name) const override {
    return llvm::StringSwitch<bool>(Name)
        .Case("sh1", true)
        .Case("sh2", true)
        .Case("sh2e", true)
        .Case("sh2a", true)
        .Case("sh2a-fpu", true)
        .Case("sh3", true)
        .Case("sh3e", true)
        .Case("sh4-nofpu", true)
        .Case("sh4", true)
        .Case("sh4a-nofpu", true)
        .Case("sh4a", true)
        .Default(false);
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override {
    Values.append({"sh1", "sh2", "sh2e", "sh2a", "sh2a-fpu", "sh3", "sh3e",
                   "sh4-nofpu", "sh4", "sh4a-nofpu", "sh4a"});
  }

  bool setCPU(const std::string &Name) override { return isValidCPUName(Name); }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::SHBuiltinVaList;
  }

  std::string_view getClobbers() const override { return ""; }

  ArrayRef<const char *> getGCCRegNames() const override {
    static const char *const GCCRegNames[] = {
        // GPRs
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
        "r11", "r12", "r13", "r14", "r15",
        // FPRs
        "fr0", "fr1", "fr2", "fr3", "fr4", "fr5", "fr6", "fr7", "fr8", "fr9",
        "fr10", "fr11", "fr12", "fr13", "fr14", "fr15",
        // Special
        "pr", "mach", "macl", "gbr", "vbr", "sr", "fpscr", "fpul"};
    return llvm::ArrayRef(GCCRegNames);
  }

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return {};
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    switch (*Name) {
    default:
      return false;
    case 'r': // General purpose register (R0-R15)
      Info.setAllowsRegister();
      return true;
    case 'f': // Floating-point register (FR0-FR15)
      Info.setAllowsRegister();
      return true;
    case 'z': // R0 register
      Info.setAllowsRegister();
      return true;
    case 'x': // MAC registers (MACH, MACL)
      Info.setAllowsRegister();
      return true;
    case 'l': // PR (procedure link register)
      Info.setAllowsRegister();
      return true;
    case 'a': // GBR (global base register)
      Info.setAllowsRegister();
      return true;
    }
  }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_SH_H
