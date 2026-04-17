//===-- SHAsmParser.cpp - Parse SH assembly to MCInst instructions ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH assembly parser.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SHMCTargetDesc.h"
#include "TargetInfo/SHTargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "sh-asm-parser"

namespace {

class SHOperand : public MCParsedAsmOperand {
  enum KindTy { k_Token, k_Register, k_Immediate } Kind;

  SMLoc StartLoc, EndLoc;

  struct Token {
    const char *Data;
    unsigned Length;
  };

  struct RegOp {
    MCRegister Reg;
  };

  struct ImmOp {
    const MCExpr *Val;
  };

  union {
    struct Token Tok;
    struct RegOp Reg;
    struct ImmOp Imm;
  };

public:
  SHOperand(KindTy K) : Kind(K) {}

  bool isToken() const override { return Kind == k_Token; }
  bool isReg() const override { return Kind == k_Register; }
  bool isImm() const override { return Kind == k_Immediate; }
  bool isMem() const override { return false; }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }

  MCRegister getReg() const override {
    assert((Kind == k_Register) && "Invalid access!");
    return Reg.Reg;
  }

  const MCExpr *getImm() const {
    assert((Kind == k_Immediate) && "Invalid access!");
    return Imm.Val;
  }

  SMLoc getStartLoc() const override { return StartLoc; }
  SMLoc getEndLoc() const override { return EndLoc; }

  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case k_Token:
      OS << "Token: " << getToken() << "\n";
      break;
    case k_Register:
      OS << "Reg: #" << getReg() << "\n";
      break;
    case k_Immediate:
      OS << "Imm: " << getImm() << "\n";
      break;
    }
  }

  static std::unique_ptr<SHOperand> createToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<SHOperand>(k_Token);
    Op->Tok.Data = Str.data();
    Op->Tok.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<SHOperand> createReg(MCRegister Reg, SMLoc S,
                                              SMLoc E) {
    auto Op = std::make_unique<SHOperand>(k_Register);
    Op->Reg.Reg = Reg;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<SHOperand> createImm(const MCExpr *Val, SMLoc S,
                                              SMLoc E) {
    auto Op = std::make_unique<SHOperand>(k_Immediate);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCExpr *Expr = getImm();
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }
};

class SHAsmParser : public MCTargetAsmParser {
  const MCRegisterInfo &MRI;

  /// @name Auto-generated Match Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "SHGenAsmMatcher.inc"

  /// }

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override {
    MCInst Inst;
    unsigned MatchResult =
        MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);
    switch (MatchResult) {
    case Match_Success:
      Inst.setLoc(IDLoc);
      Out.emitInstruction(Inst, getSTI());
      return false;
    case Match_MissingFeature:
      return Error(IDLoc,
                   "instruction requires a CPU feature not currently enabled");
    case Match_InvalidOperand: {
      SMLoc ErrorLoc = IDLoc;
      if (ErrorInfo != ~0ULL) {
        if (ErrorInfo >= Operands.size())
          return Error(IDLoc, "too few operands for instruction");
        ErrorLoc = ((SHOperand &)*Operands[ErrorInfo]).getStartLoc();
        if (ErrorLoc == SMLoc())
          ErrorLoc = IDLoc;
      }
      return Error(ErrorLoc, "invalid operand for instruction");
    }
    case Match_MnemonicFail:
      return Error(IDLoc, "invalid instruction mnemonic");
    }
    llvm_unreachable("Implement any new match types added!");
  }

  bool parseRegister(MCRegister &RegNo, SMLoc &StartLoc,
                     SMLoc &EndLoc) override {
    if (!tryParseRegister(RegNo, StartLoc, EndLoc).isSuccess())
      return Error(StartLoc, "invalid register name");
    return false;
  }

  ParseStatus tryParseRegister(MCRegister &RegNo, SMLoc &StartLoc,
                               SMLoc &EndLoc) override {
    if (getLexer().getKind() != AsmToken::Plus &&
        getLexer().getKind() != AsmToken::Minus &&
        getLexer().getKind() != AsmToken::Percent)
      return ParseStatus::NoMatch;

    StartLoc = getParser().getTok().getLoc();
    if (getLexer().getKind() == AsmToken::Percent)
      getParser().Lex(); // Eat the '%'

    if (getLexer().getKind() != AsmToken::Identifier)
      return ParseStatus::NoMatch;

    unsigned RegKind;
    MCRegister Reg = matchRegisterName(getParser().getTok(), RegKind);
    if (!Reg)
      return ParseStatus::NoMatch;

    getParser().Lex(); // Eat identifier token
    EndLoc = getParser().getTok().getLoc();
    RegNo = Reg;
    return ParseStatus::Success;
  }

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override {
    Operands.push_back(SHOperand::createToken(Name, NameLoc));

    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      if (!parseOperand(Operands, Name).isSuccess()) {
        SMLoc Loc = getLexer().getLoc();
        return Error(Loc, "unexpected token");
      }

      while (getLexer().is(AsmToken::Comma)) {
        getParser().Lex(); // Eat the comma
        if (!parseOperand(Operands, Name).isSuccess()) {
          SMLoc Loc = getLexer().getLoc();
          return Error(Loc, "unexpected token");
        }
      }
    }
    if (getLexer().isNot(AsmToken::EndOfStatement))
      return Error(getLexer().getLoc(), "unexpected token");
    getParser().Lex(); // Consume EndOfStatement
    return false;
  }

  ParseStatus parseOperand(OperandVector &Operands, StringRef Mnemonic) {
    if (getLexer().is(AsmToken::Hash)) {
      Operands.push_back(
          SHOperand::createToken("#", getParser().getTok().getLoc()));
      getParser().Lex(); // Eat #

      std::unique_ptr<SHOperand> Op;
      if (!parseSHAsmOperand(Op).isSuccess() || !Op)
        return ParseStatus::Failure;
      Operands.push_back(std::move(Op));
      return ParseStatus::Success;
    }

    if (getLexer().is(AsmToken::At)) {
      // SH addressing modes using @ prefix.
      // The AsmMatcher uses compound tokens, so we must produce:
      //   @      → "@"          (for @rn, @rn+)
      //   @(     → "@("         (for @(disp, rn), @(disp, pc))
      //   @(r0   → "@(r0"       (for @(r0, rn))
      //   @-     → "@-"         (for @-rn)
      //   pc)    → "pc)"        (inside @(disp, pc))
      //   gbr)   → "gbr)"       (inside @(disp, gbr))
      //   )      → ")"          (inside @(disp, rn) / @(r0, rn))
      SMLoc AtLoc = getParser().getTok().getLoc();
      getParser().Lex(); // Eat @

      if (getLexer().is(AsmToken::LParen)) {
        getParser().Lex(); // Eat (

        // Check for @(r0, rn) — r0-indexed addressing
        // The AsmMatcher expects compound token "@(r0" for this form.
        if (getLexer().is(AsmToken::Identifier)) {
          StringRef Ident = getParser().getTok().getString();
          if (Ident.equals_insensitive("r0")) {
            // Peek ahead: if next is comma, this is @(r0, rn)
            SMLoc R0Loc = getParser().getTok().getLoc();
            getParser().Lex(); // Eat r0
            if (getLexer().is(AsmToken::Comma)) {
              Operands.push_back(SHOperand::createToken("@(r0", AtLoc));
              getParser().Lex(); // Eat comma
              // Parse the second register
              std::unique_ptr<SHOperand> Op;
              if (!parseSHAsmOperand(Op).isSuccess())
                return ParseStatus::Failure;
              Operands.push_back(std::move(Op));
              // Expect closing paren
              if (!getLexer().is(AsmToken::RParen))
                return ParseStatus::Failure;
              Operands.push_back(
                  SHOperand::createToken(")", getParser().getTok().getLoc()));
              getParser().Lex(); // Eat )
              return ParseStatus::Success;
            }
            // Not @(r0, rn) — push back r0 as the displacement part
            // This is @(r0-as-imm, something) which shouldn't happen.
            // Fall through treating r0 as a regular operand.
            // We already ate r0, so create a reg operand for it.
            MCRegister Reg = SH::R0;
            Operands.push_back(SHOperand::createToken("@(", AtLoc));
            Operands.push_back(SHOperand::createReg(Reg, R0Loc, R0Loc));
            // Continue to parse the rest
            if (getLexer().is(AsmToken::RParen)) {
              Operands.push_back(
                  SHOperand::createToken(")", getParser().getTok().getLoc()));
              getParser().Lex();
            }
            return ParseStatus::Success;
          }
        }

        // @(disp, rn/pc/gbr) — displacement-based addressing
        Operands.push_back(SHOperand::createToken("@(", AtLoc));

        // Parse displacement (immediate or symbol expression)
        std::unique_ptr<SHOperand> Op;
        if (!parseSHAsmOperand(Op).isSuccess())
          return ParseStatus::Failure;
        Operands.push_back(std::move(Op));

        if (getLexer().is(AsmToken::Comma)) {
          getParser()
              .Lex(); // Eat comma (consumed silently — AsmMatcher separator)

          // Parse second part: register, or special tokens pc/gbr
          if (getLexer().is(AsmToken::Identifier)) {
            StringRef Ident = getParser().getTok().getString();
            if (Ident.equals_insensitive("pc")) {
              // Expect closing paren immediately after pc
              getParser().Lex(); // Eat pc
              if (!getLexer().is(AsmToken::RParen))
                return ParseStatus::Failure;
              Operands.push_back(
                  SHOperand::createToken("pc)", getParser().getTok().getLoc()));
              getParser().Lex(); // Eat )
              return ParseStatus::Success;
            }
            if (Ident.equals_insensitive("gbr")) {
              getParser().Lex(); // Eat gbr
              if (!getLexer().is(AsmToken::RParen))
                return ParseStatus::Failure;
              Operands.push_back(SHOperand::createToken(
                  "gbr)", getParser().getTok().getLoc()));
              getParser().Lex(); // Eat )
              return ParseStatus::Success;
            }
          }
          // Regular register as second operand
          if (!parseSHAsmOperand(Op).isSuccess())
            return ParseStatus::Failure;
          Operands.push_back(std::move(Op));
        }

        if (!getLexer().is(AsmToken::RParen))
          return ParseStatus::Failure;
        Operands.push_back(
            SHOperand::createToken(")", getParser().getTok().getLoc()));
        getParser().Lex(); // Eat )
        return ParseStatus::Success;
      }

      if (getLexer().is(AsmToken::Minus)) {
        Operands.push_back(SHOperand::createToken("@-", AtLoc));
        getParser().Lex(); // Eat -
        // Parse register after @-
        std::unique_ptr<SHOperand> Op;
        if (!parseSHAsmOperand(Op).isSuccess())
          return ParseStatus::Failure;
        Operands.push_back(std::move(Op));
        return ParseStatus::Success;
      }

      // Plain @rn or @rn+
      Operands.push_back(SHOperand::createToken("@", AtLoc));
      std::unique_ptr<SHOperand> Op;
      if (!parseSHAsmOperand(Op).isSuccess())
        return ParseStatus::Failure;
      Operands.push_back(std::move(Op));

      if (getLexer().is(AsmToken::Plus)) {
        Operands.push_back(
            SHOperand::createToken("+", getParser().getTok().getLoc()));
        getParser().Lex(); // Eat +
      }

      return ParseStatus::Success;
    }

    // Check for banked register names (r0_bank through r7_bank).
    // These are literal tokens in the instruction asm string, not GPRs.
    if (getLexer().is(AsmToken::Identifier)) {
      StringRef Ident = getParser().getTok().getString();
      if (Ident.ends_with_insensitive("_bank")) {
        SMLoc S = getParser().getTok().getLoc();
        Operands.push_back(SHOperand::createToken(Ident, S));
        getParser().Lex(); // Eat the identifier
        return ParseStatus::Success;
      }
    }

    std::unique_ptr<SHOperand> Op;
    ParseStatus Res = parseSHAsmOperand(Op);
    if (!Res.isSuccess() || !Op)
      return ParseStatus::Failure;
    Operands.push_back(std::move(Op));
    return ParseStatus::Success;
  }

  ParseStatus parseSHAsmOperand(std::unique_ptr<SHOperand> &Op) {
    SMLoc S = getParser().getTok().getLoc();
    SMLoc E =
        SMLoc::getFromPointer(getParser().getTok().getLoc().getPointer() - 1);
    const MCExpr *EVal;

    Op = nullptr;
    switch (getLexer().getKind()) {
    default:
      break;
    case AsmToken::Identifier:
      // Try parsing as register without `%` prefix (like `r0`)
      unsigned RegKind;
      if (MCRegister Reg = matchRegisterName(getParser().getTok(), RegKind)) {
        getParser().Lex(); // Eat identifier token.
        E = SMLoc::getFromPointer(getParser().getTok().getLoc().getPointer() -
                                  1);
        Op = SHOperand::createReg(Reg, S, E);
        break;
      }
      [[fallthrough]];
    case AsmToken::Minus:
    case AsmToken::Integer:
    case AsmToken::LParen:
    case AsmToken::Dot:
      if (getParser().parseExpression(EVal, E))
        break;
      Op = SHOperand::createImm(EVal, S, E);
      break;
    }
    return Op ? ParseStatus::Success : ParseStatus::Failure;
  }

  MCRegister matchRegisterName(const AsmToken &Tok, unsigned &RegKind);

public:
  SHAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
              const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII),
        MRI(*Parser.getContext().getRegisterInfo()) {
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

} // end anonymous namespace

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "SHGenAsmMatcher.inc"

MCRegister SHAsmParser::matchRegisterName(const AsmToken &Tok,
                                          unsigned &RegKind) {
  if (!Tok.is(AsmToken::Identifier))
    return 0;

  StringRef Name = Tok.getString();
  MCRegister Reg = MatchRegisterName(Name.lower());
  // Try without prefix if it fails
  if (!Reg && Name.starts_with_insensitive("r")) {
    int64_t RegNo = 0;
    if (!Name.substr(1).getAsInteger(10, RegNo) && RegNo < 16) {
      return MatchRegisterName(("r" + Twine(RegNo)).str().c_str());
    }
  }
  if (!Reg && Name.starts_with_insensitive("fr")) {
    int64_t RegNo = 0;
    if (!Name.substr(2).getAsInteger(10, RegNo) && RegNo < 16) {
      return MatchRegisterName(("fr" + Twine(RegNo)).str().c_str());
    }
  }
  return Reg;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSHAsmParser() {
  RegisterMCAsmParser<SHAsmParser> X(getTheSHTarget());
}
