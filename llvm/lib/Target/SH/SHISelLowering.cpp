//===- SHISelLowering.cpp - SH DAG Lowering Implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SH DAG lowering interface.
//
//===----------------------------------------------------------------------===//

#include "SHISelLowering.h"
#include "SH.h"
#include "SHConstantPoolValue.h"
#include "SHMachineFunctionInfo.h"
#include "SHSubtarget.h"
#include "SHTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "sh-isel-lower"

using namespace llvm;

#define GET_CALLINGCONV_IMPL
#include "SHGenCallingConv.inc"

SHTargetLowering::SHTargetLowering(const TargetMachine &TM,
                                   const SHSubtarget &Subtarget)
    : TargetLowering(TM, Subtarget), Subtarget(Subtarget) {
  // Set up register class for i32.
  addRegisterClass(MVT::i32, &SH::GPRRegClass);
  if (Subtarget.hasFPU())
    addRegisterClass(MVT::f32, &SH::FPR32RegClass);
  if (Subtarget.hasDPFPU())
    addRegisterClass(MVT::f64, &SH::FPR64RegClass);

  // Compute derived properties.
  computeRegisterProperties(Subtarget.getRegisterInfo());

  // How we deal with i1
  // SH's MOVT instruction produces exactly 0 or 1 from the T-bit.
  // UndefinedBooleanContent causes the DAGCombiner to drop AND masks
  // during i1→i32 type promotion, miscompiling patterns like
  // (and (load i8), 1) == 0  →  (load i8) == 0 (WRONG!)
  // This is critical for ctype functions (isupper, islower, etc.).
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);

  setStackPointerRegisterToSaveRestore(SH::R15);
  // All atomic ops lower to libcalls for every SH variant we support today.
  // SH4A exposes movli.l/movco.l for inline LL/SC, but no ISel patterns for
  // AtomicLoad/AtomicStore/AtomicRMW are wired up yet, so raising the limit
  // here would strand selection. Revisit when native SH4A atomics land.
  setMaxAtomicSizeInBitsSupported(0);

  // Expand all atomics to libcalls.
  for (unsigned Opc = 0; Opc < ISD::BUILTIN_OP_END; ++Opc) {
    if (Opc >= ISD::ATOMIC_CMP_SWAP && Opc <= ISD::ATOMIC_LOAD_UMAX) {
      setOperationAction(Opc, MVT::i8, Expand);
      setOperationAction(Opc, MVT::i16, Expand);
      setOperationAction(Opc, MVT::i32, Expand);
    }
  }

  // setIndexedLoadAction(ISD::POST_INC, MVT::i8, Legal);
  // setIndexedLoadAction(ISD::POST_INC, MVT::i16, Legal);
  // setIndexedLoadAction(ISD::POST_INC, MVT::i32, Legal);
  // setIndexedLoadAction(ISD::POST_INC, MVT::f32, Legal);

  // setIndexedStoreAction(ISD::PRE_DEC, MVT::i8, Legal);
  // setIndexedStoreAction(ISD::PRE_DEC, MVT::i16, Legal);
  // setIndexedStoreAction(ISD::PRE_DEC, MVT::i32, Legal);
  // setIndexedStoreAction(ISD::PRE_DEC, MVT::f32, Legal);

  // Operations that need custom lowering.
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setOperationAction(ISD::ExternalSymbol, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i32, Custom);
  setOperationAction(ISD::JumpTable, MVT::i32, Custom);
  setOperationAction(ISD::ConstantPool, MVT::i32, Custom);
  setOperationAction(ISD::Constant, MVT::i32, Custom);
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  setOperationAction(ISD::SHL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRA_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRL_PARTS, MVT::i32, Expand);

  setOperationAction(ISD::SDIV, MVT::i32, Expand);
  setOperationAction(ISD::UDIV, MVT::i32, Expand);
  // SH libgcc lacks __modsi3/__umodsi3, so we custom-lower
  // SREM/UREM to: a - (a / b) * b, using the available
  // __sdivsi3/__udivsi3 for the division.
  setOperationAction(ISD::SREM, MVT::i32, Custom);
  setOperationAction(ISD::UREM, MVT::i32, Custom);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);

  setOperationAction(ISD::SETCC, MVT::i32, Custom);
  setOperationAction(ISD::SETCC, MVT::f32, Custom);
  setOperationAction(ISD::SETCC, MVT::f64, Custom);
  setOperationAction(ISD::BR_CC, MVT::f32, Expand);
  setOperationAction(ISD::BR_CC, MVT::f64, Expand);

  // Floating point conditions that need expansion
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setCondCodeAction(ISD::SETO, VT, Expand);
    setCondCodeAction(ISD::SETUO, VT, Expand);
    setCondCodeAction(ISD::SETOEQ, VT, Custom);
    setCondCodeAction(ISD::SETOGT, VT, Custom);
    setCondCodeAction(ISD::SETOGE, VT, Custom);
    setCondCodeAction(ISD::SETOLT, VT, Custom); // Swapped
    setCondCodeAction(ISD::SETOLE, VT, Custom);
    setCondCodeAction(ISD::SETONE, VT, Expand);
    setCondCodeAction(ISD::SETUEQ, VT, Expand);
    setCondCodeAction(ISD::SETUGT, VT, Expand);
    setCondCodeAction(ISD::SETUGE, VT, Expand);
    setCondCodeAction(ISD::SETULT, VT, Expand);
    setCondCodeAction(ISD::SETULE, VT, Expand);
    setCondCodeAction(ISD::SETUNE, VT, Expand);
  }

  // We don't expand SELECT here; we make SELECT Legal and lower it
  // into Pseudos (Select_i32 etc.) via TableGen, then expand in CustomInserter.
  // But SELECT_CC should be expanded to SETCC + SELECT to avoid ISel failure.
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Expand);

  setOperationAction(ISD::BRCOND, MVT::Other, Custom);

  // Emit TRAPA #0 for __builtin_trap() and unreachable code.
  setOperationAction(ISD::TRAP, MVT::Other, Legal);

  setOperationAction(ISD::FCOPYSIGN, MVT::f32, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f64, Expand);

  // We have TableGen patterns for float-to-int via FPUL, so we leave it Legal.
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Expand);
  setOperationAction(ISD::FP_TO_UINT, MVT::i64, Expand);
  setOperationAction(ISD::UINT_TO_FP, MVT::i32, Expand);
  setOperationAction(ISD::UINT_TO_FP, MVT::i64, Expand);

  // Expand unsupported floating point condition codes
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setCondCodeAction(ISD::SETO, VT, Expand);
    setCondCodeAction(ISD::SETUO, VT, Expand);
    setCondCodeAction(ISD::SETUEQ, VT, Expand);
    setCondCodeAction(ISD::SETONE, VT, Expand);
    // SH float comapre instruction are fcmp/eq and fcmp/gt.
    // They are ordered. Unordered branches (like SETULT, SETULE) should be
    // expanded into ordered branches + UNO check, OR we lower them to integer
    // compares? Wait, let LLVM expand them by default:
    setCondCodeAction(ISD::SETULT, VT, Expand);
    setCondCodeAction(ISD::SETULE, VT, Expand);
    setCondCodeAction(ISD::SETUGT, VT, Expand);
    setCondCodeAction(ISD::SETUGE, VT, Expand);

    // Expand floating-point math intrinsics that SH does not support in
    // hardware
    setOperationAction(ISD::FSIN, VT, Expand);
    setOperationAction(ISD::FCOS, VT, Expand);
    setOperationAction(ISD::FSINCOS, VT, Expand);
    setOperationAction(ISD::FPOW, VT, Expand);
    setOperationAction(ISD::FPOWI, VT, Expand);
    setOperationAction(ISD::FREM, VT, Expand);
    setOperationAction(ISD::FEXP, VT, Expand);
    setOperationAction(ISD::FEXP2, VT, Expand);
    setOperationAction(ISD::FEXP10, VT, Expand);
    setOperationAction(ISD::FLOG, VT, Expand);
    setOperationAction(ISD::FLOG2, VT, Expand);
    setOperationAction(ISD::FLOG10, VT, Expand);
    setOperationAction(ISD::FCEIL, VT, Expand);
    setOperationAction(ISD::FTRUNC, VT, Expand);
    setOperationAction(ISD::FRINT, VT, Expand);
    setOperationAction(ISD::FNEARBYINT, VT, Expand);
    setOperationAction(ISD::FROUND, VT, Expand);
    setOperationAction(ISD::FROUNDEVEN, VT, Expand);
    setOperationAction(ISD::FFLOOR, VT, Expand);
    setOperationAction(ISD::FMINNUM, VT, Expand);
    setOperationAction(ISD::FMAXNUM, VT, Expand);
    setOperationAction(ISD::FMINIMUM, VT, Expand);
    setOperationAction(ISD::FMAXIMUM, VT, Expand);
    setOperationAction(ISD::LROUND, VT, Expand);
    setOperationAction(ISD::LLROUND, VT, Expand);
    setOperationAction(ISD::LRINT, VT, Expand);
    setOperationAction(ISD::LLRINT, VT, Expand);
  }

  setOperationAction(ISD::FRAMEADDR, MVT::i32, Custom);
  setOperationAction(ISD::RETURNADDR, MVT::i32, Custom);
  // setOperationAction(ISD::FrameIndex,     MVT::i32, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);

  // Expand stack save and restore
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  setStackPointerRegisterToSaveRestore(SH::R15);

  // VASTART needs to be custom lowered to use the frame index
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);

  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  // FMA is expanded (fmul + fadd) because SH's fmac requires FR0
  setOperationAction(ISD::FMA, MVT::f32, Expand);
  setOperationAction(ISD::FMA, MVT::f64, Expand);

  setOperationAction(ISD::ROTL, MVT::i32, Expand);
  setOperationAction(ISD::ROTR, MVT::i32, Expand);
  setOperationAction(ISD::CTPOP, MVT::i32, Expand);
  setOperationAction(ISD::CTLZ, MVT::i32, Expand);
  setOperationAction(ISD::CTTZ, MVT::i32, Expand);
  setOperationAction(ISD::UDIV, MVT::i32, Expand);
  setOperationAction(ISD::SDIV, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);

  // Expand i1 sign extension.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // Sign/zero extending loads are handled by specific instructions.
  // Integer EXTLOAD/ZEXTLOAD should not be expanded to avoid infinite loops.
  // Instead, they should be matched in instruction selection using Pat<> or
  // lowered via Custom.

  // No extending float loads
  // MVT::f64 from MVT::f32 must be expanded into FLDS and FCNVSD
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i8, Legal);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i16, Legal);
  setLoadExtAction(ISD::EXTLOAD, MVT::i32, MVT::i1, Promote);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i1, Promote);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i1, Promote);

  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(4));
}

const char *SHTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case SHISD::RET:
    return "SHISD::RET";
  case SHISD::CALL:
    return "SHISD::CALL";
  case SHISD::CALLR:
    return "SHISD::CALLR";
  case SHISD::WRAPPER:
    return "SHISD::WRAPPER";
  case SHISD::BRCC:
    return "SHISD::BRCC";
  case SHISD::GOTAddr:
    return "SHISD::GOTAddr";
  case SHISD::GlobalAddressPIC:
    return "SHISD::GlobalAddressPIC";
  case SHISD::READ_GBR:
    return "SHISD::READ_GBR";
  case SHISD::WrapperTLS:
    return "SHISD::WrapperTLS";
  case SHISD::EH_RETURN:
    return "SHISD::EH_RETURN";
  case SHISD::SHLD:
    return "SHISD::SHLD";
  case SHISD::SHAD:
    return "SHISD::SHAD";
  case SHISD::CMP:
    return "SHISD::CMP";
  case SHISD::MOVT:
    return "SHISD::MOVT";
  case SHISD::CPI_ADDR:
    return "SHISD::CPI_ADDR";
  default: {
    static char buf[64];
    snprintf(buf, sizeof(buf), "UNKNOWN_DAG_OPC_%u", Opcode);
    return buf;
  }
  }
}

bool SHTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                             const AddrMode &AM, Type *Ty,
                                             unsigned AS,
                                             Instruction *I) const {
  // SH supports: @Rn, @(disp,Rn), @(R0,Rn)
  if (AM.HasBaseReg && AM.BaseOffs == 0 && !AM.BaseGV)
    return true; // @Rn
  if (AM.HasBaseReg && AM.BaseOffs != 0 && !AM.BaseGV)
    return true; // @(disp,Rn) — we handle range in eliminateFrameIndex
  return false;
}

bool SHTargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                  SDValue &Base,
                                                  SDValue &Offset,
                                                  ISD::MemIndexedMode &AM,
                                                  SelectionDAG &DAG) const {
  if (Op->getOpcode() != ISD::ADD)
    return false;

  if (auto *C = dyn_cast<ConstantSDNode>(Op->getOperand(1))) {
    unsigned ElemSize = 0;
    if (auto *LD = dyn_cast<LoadSDNode>(N))
      ElemSize = LD->getMemoryVT().getStoreSize();
    else if (auto *ST = dyn_cast<StoreSDNode>(N))
      ElemSize = ST->getMemoryVT().getStoreSize();

    if (ElemSize > 0 && C->getZExtValue() == ElemSize) {
      Base = Op->getOperand(0);
      Offset = Op->getOperand(1);
      AM = ISD::POST_INC;
      return true;
    }
  }
  return false;
}

bool SHTargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                                 SDValue &Offset,
                                                 ISD::MemIndexedMode &AM,
                                                 SelectionDAG &DAG) const {
  SDValue Ptr;
  if (auto *LD = dyn_cast<LoadSDNode>(N))
    Ptr = LD->getBasePtr();
  else if (auto *ST = dyn_cast<StoreSDNode>(N))
    Ptr = ST->getBasePtr();
  else
    return false;

  if (Ptr.getOpcode() != ISD::SUB && Ptr.getOpcode() != ISD::ADD)
    return false;

  if (auto *C = dyn_cast<ConstantSDNode>(Ptr.getOperand(1))) {
    unsigned ElemSize = 0;
    if (auto *LD = dyn_cast<LoadSDNode>(N))
      ElemSize = LD->getMemoryVT().getStoreSize();
    else if (auto *ST = dyn_cast<StoreSDNode>(N))
      ElemSize = ST->getMemoryVT().getStoreSize();

    int64_t Val =
        (Ptr.getOpcode() == ISD::SUB) ? -C->getSExtValue() : C->getSExtValue();
    if (ElemSize > 0 && Val == -(int64_t)ElemSize) {
      Base = Ptr.getOperand(0);
      Offset = Ptr.getOperand(1);
      AM = ISD::PRE_DEC;
      return true;
    }
  }
  return false;
}

SDValue SHTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress:
    return LowerGlobalTLSAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return LowerExternalSymbol(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::JumpTable:
    return LowerJumpTable(Op, DAG);
  case ISD::ConstantPool:
    return LowerConstantPool(Op, DAG);
  case ISD::Constant:
    return LowerConstant(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::BRCOND:
    return LowerBRCOND(Op, DAG);
  case ISD::SETCC:
    return LowerSETCC(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::FRAMEADDR:
    return LowerFRAMEADDR(Op, DAG);
  case ISD::RETURNADDR:
    return LowerRETURNADDR(Op, DAG);
  case ISD::FrameIndex:
    return LowerFrameIndex(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::VACOPY:
    return LowerVACOPY(Op, DAG);
  case ISD::EH_RETURN:
    return LowerEH_RETURN(Op, DAG);
  case ISD::SREM:
  case ISD::UREM:
    return LowerREM(Op, DAG);
  default:
    llvm_unreachable("Unexpected operation to lower.");
  }
}

//===----------------------------------------------------------------------===//
// Address lowering helpers
//===----------------------------------------------------------------------===//

static SDValue wrapAddress(SDValue Addr, SDLoc DL, SelectionDAG &DAG) {
  // SH needs addresses to be placed in a constant pool and loaded
  // via MOVL_PCREL.  All address types are routed through
  // MachineConstantPool entries so the Constant Island pass can
  // place them within reach.

  // Already a ConstantPoolIndex — just wrap it.
  if (Addr.getOpcode() == ISD::TargetConstantPool)
    return DAG.getNode(SHISD::WRAPPER, DL, MVT::i32, Addr);

  EVT PtrVT = Addr.getValueType();
  int64_t Offset = 0;
  SDValue CP;

  if (auto *G = dyn_cast<GlobalAddressSDNode>(Addr)) {
    Offset = G->getOffset();
    CP = DAG.getTargetConstantPool(G->getGlobal(), PtrVT, Align(4), 0);
  } else if (auto *B = dyn_cast<BlockAddressSDNode>(Addr)) {
    Offset = B->getOffset();
    CP = DAG.getTargetConstantPool(B->getBlockAddress(), PtrVT, Align(4), 0);
  } else if (isa<JumpTableSDNode>(Addr)) {
    // Keep the JTI operand directly on the WRAPPER→MOV_I32 instruction.
    // The BranchFolder uses JTI operands to determine which jump tables
    // are alive, so they must remain visible until after that pass runs.
    // The Constant Island pass will later convert the MOV_I32/JTI to
    // MOV_I32/CPI (via SHConstantPoolJTI), ensuring the address is
    // within PC-relative reach.
    return DAG.getNode(SHISD::WRAPPER, DL, MVT::i32, Addr);
  } else if (auto *ES = dyn_cast<ExternalSymbolSDNode>(Addr)) {
    // Wrap the external symbol in a SHConstantPoolSymbol.
    auto *CPV =
        SHConstantPoolSymbol::Create(*DAG.getContext(), ES->getSymbol());
    CP = DAG.getTargetConstantPool(CPV, PtrVT, Align(4), 0);
  } else {
    // Unknown address type — wrap directly (fallback).
    return DAG.getNode(SHISD::WRAPPER, DL, MVT::i32, Addr);
  }

  SDValue WrappedAddr = DAG.getNode(SHISD::WRAPPER, DL, PtrVT, CP);
  if (Offset != 0) {
    SDValue OffsNode = DAG.getConstant(Offset, DL, PtrVT);
    WrappedAddr = DAG.getNode(ISD::ADD, DL, PtrVT, WrappedAddr, OffsNode);
  }
  return WrappedAddr;
}

SDValue SHTargetLowering::LowerGlobalAddress(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  if (DAG.getTarget().isPositionIndependent()) {
    MachineFunction &MF = DAG.getMachineFunction();

    // GOT base: create CPI for _GLOBAL_OFFSET_TABLE_@GOTPC.
    // MOV_GOT expands to: mova @CPI + mov.l @CPI + add R0 (6 bytes fixed).
    auto *GOTCPV = SHConstantPoolSymbol::Create(
        *DAG.getContext(), "_GLOBAL_OFFSET_TABLE_", SHCP::GOTPC);
    unsigned GOTCPI =
        MF.getConstantPool()->getConstantPoolIndex(GOTCPV, Align(4));
    SDValue GOTCP =
        DAG.getTargetConstantPool(GOTCPV, PtrVT, Align(4), 0, GOTCPI);
    SDValue GOTAddr = DAG.getNode(SHISD::GOTAddr, DL, PtrVT, GOTCP);

    // GOT offset for this symbol: route through CPI with GOT modifier.
    auto *CPV = SHConstantPoolGV::Create(N->getGlobal(), SHCP::GOT);
    unsigned CPI = MF.getConstantPool()->getConstantPoolIndex(CPV, Align(4));
    SDValue CP = DAG.getTargetConstantPool(CPV, PtrVT, Align(4), 0, CPI);
    SDValue Offset = DAG.getNode(SHISD::WRAPPER, DL, PtrVT, CP);

    SDValue GOTEntry = DAG.getNode(ISD::ADD, DL, PtrVT, GOTAddr, Offset);
    return DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), GOTEntry,
                       MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  }

  SDValue TargetAddr =
      DAG.getTargetGlobalAddress(N->getGlobal(), DL, PtrVT, N->getOffset());
  return wrapAddress(TargetAddr, DL, DAG);
}

SDValue SHTargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  SDValue GBR = DAG.getNode(SHISD::READ_GBR, DL, PtrVT);

  // TP offset for this symbol: route through CPI with TPOFF modifier.
  // emitMachineConstantPoolValue emits '.long sym' (linker resolves @TPOFF).
  MachineFunction &MF = DAG.getMachineFunction();
  auto *CPV = SHConstantPoolGV::Create(N->getGlobal(), SHCP::TPOFF);
  unsigned CPI = MF.getConstantPool()->getConstantPoolIndex(CPV, Align(4));
  SDValue CP = DAG.getTargetConstantPool(CPV, PtrVT, Align(4), 0, CPI);
  SDValue Offset = DAG.getNode(SHISD::WRAPPER, DL, PtrVT, CP);

  return DAG.getNode(ISD::ADD, DL, PtrVT, GBR, Offset);
}

SDValue SHTargetLowering::LowerExternalSymbol(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const ExternalSymbolSDNode *N = cast<ExternalSymbolSDNode>(Op);
  SDValue ES = DAG.getTargetExternalSymbol(N->getSymbol(), MVT::i32);
  return wrapAddress(ES, DL, DAG);
}

SDValue SHTargetLowering::LowerBlockAddress(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const BlockAddressSDNode *N = cast<BlockAddressSDNode>(Op);
  SDValue BA = DAG.getTargetBlockAddress(N->getBlockAddress(), MVT::i32);
  return wrapAddress(BA, DL, DAG);
}

SDValue SHTargetLowering::LowerJumpTable(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const JumpTableSDNode *N = cast<JumpTableSDNode>(Op);
  SDValue JT = DAG.getTargetJumpTable(N->getIndex(), MVT::i32);
  return wrapAddress(JT, DL, DAG);
}

SDValue SHTargetLowering::LowerConstantPool(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const ConstantPoolSDNode *N = cast<ConstantPoolSDNode>(Op);
  SDValue CP;
  if (N->isMachineConstantPoolEntry())
    CP = DAG.getTargetConstantPool(N->getMachineCPVal(), MVT::i32,
                                   N->getAlign(), N->getOffset());
  else
    CP = DAG.getTargetConstantPool(N->getConstVal(), MVT::i32, N->getAlign(),
                                   N->getOffset());

  // WRAPPER + MOVL_PCREL loads the CPI DATA directly into a GPR. This is
  // correct only when the CPI entry stores an address (e.g., a ConstantInt
  // used as a large immediate value).
  // For all other types — ConstantFP, ConstantDataArray (lookup tables),
  // ConstantVector, etc. — the code needs the ADDRESS of the data, not the
  // data itself.  We create a second CPI entry wrapping the first's address
  // via SHConstantPoolCPIAddr, so the CI pass places both near users.
  if (!N->isMachineConstantPoolEntry()) {
    const Constant *C = N->getConstVal();
    if (!isa<ConstantInt>(C)) {
      // Add the FP data to MachineConstantPool to get a CPI index.
      MachineFunction &MF = DAG.getMachineFunction();
      unsigned DataCPI =
          MF.getConstantPool()->getConstantPoolIndex(C, N->getAlign());
      // Create a second CPI entry that will emit '.long .LCPI_DataCPI'.
      auto *CPV = SHConstantPoolCPIAddr::Create(*DAG.getContext(), DataCPI);
      SDValue AddrCP = DAG.getTargetConstantPool(CPV, MVT::i32, Align(4), 0);
      return wrapAddress(AddrCP, DL, DAG);
    }
  }

  return wrapAddress(CP, DL, DAG);
}

SDValue SHTargetLowering::LowerConstant(SDValue Op, SelectionDAG &DAG) const {
  const ConstantSDNode *CN = cast<ConstantSDNode>(Op);
  int64_t Val = CN->getSExtValue();
  // If it fits in 8 bits signed, let it match MOV_I (i8imm) natively.
  if (isInt<8>(Val))
    return Op;

  SDLoc dl(Op);
  // Otherwise, put it in the constant pool!
  SDValue CP = DAG.getConstantPool(
      ConstantInt::get(*DAG.getContext(), CN->getAPIntValue()),
      getPointerTy(DAG.getDataLayout()));
  return CP;
}

//===----------------------------------------------------------------------===//
// Condition code translation
//===----------------------------------------------------------------------===//

/// Translate an ISD::CondCode to an SHCC code and optionally swap LHS/RHS.
/// SH only has CMP/EQ, CMP/GT, CMP/GE, CMP/HI, CMP/HS natively.
/// We use the SHCC::LT and SHCC::LE codes as abstract condition codes
/// that are expanded to CMP/GT + swap (or CMP/GE + swap) in both the
/// BRCC pseudo expansion and the CMP ISel.
/// Returns true if the condition code is supported.
static bool translateCondCode(ISD::CondCode CC, SHCC::CondCode &TargetCC,
                              bool &Swap, bool &InvertBranch) {
  Swap = false;
  InvertBranch = false;
  switch (CC) {
  default:
    return false; // Unsupported, let LegalizeDAG expand it.
  case ISD::SETEQ:
  case ISD::SETOEQ:
    TargetCC = SHCC::EQ;
    return true;
  case ISD::SETNE:
  case ISD::SETUNE:
    InvertBranch = true;
    TargetCC = SHCC::EQ;
    return true;
  case ISD::SETLT:
  case ISD::SETOLT:
    TargetCC = SHCC::LT;
    return true;
  case ISD::SETLE:
    TargetCC = SHCC::LE;
    return true;
  // SETOLE(a,b) = NOT(a > b) = NOT(GT(a,b)): swap so GT(b,a)=LT, then invert.
  // SH's FCMP/GT clears T for NaN, so NOT(GT) = SETULE (unordered LE), which
  // is fine for non-NaN inputs. GCC uses the same approach.
  case ISD::SETOLE:
    Swap = true;
    InvertBranch = true;
    TargetCC = SHCC::LT;
    return true;
  case ISD::SETGT:
  case ISD::SETOGT:
    Swap = true;
    TargetCC = SHCC::LT;
    return true; // GT(a,b) = LT(b,a)
  case ISD::SETGE:
    Swap = true;
    TargetCC = SHCC::LE;
    return true; // GE(a,b) = LE(b,a)
  // SETOGE(a,b) = NOT(a < b) = NOT(LT(a,b)): use LT, then invert.
  case ISD::SETOGE:
    InvertBranch = true;
    TargetCC = SHCC::LT;
    return true;
  case ISD::SETULT:
    TargetCC = SHCC::ULT;
    return true;
  case ISD::SETULE:
    TargetCC = SHCC::ULE;
    return true;
  case ISD::SETUGT:
    Swap = true;
    TargetCC = SHCC::ULT;
    return true;
  case ISD::SETUGE:
    Swap = true;
    TargetCC = SHCC::ULE;
    return true;
  }
}

SDValue SHTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  // When the DAG combiner folds SETCC(f64/f32) + BRCOND into a BR_CC
  // with float operands, we must lower through SETCC + BRCOND rather
  // than creating a BRCC pseudo with float register operands.  The
  // BRCC pseudo expects GPR operands and its expansion emits integer
  // CMP instructions; passing float regs produces incorrect codegen.
  EVT CmpVT = LHS.getValueType();
  if (CmpVT == MVT::f32 || CmpVT == MVT::f64) {
    SDValue SetCC =
        DAG.getNode(ISD::SETCC, DL, MVT::i32, LHS, RHS, DAG.getCondCode(CC));
    return DAG.getNode(ISD::BRCOND, DL, MVT::Other, Chain, SetCC, Dest);
  }

  bool Swap, Invert;
  SHCC::CondCode SHCC;
  if (!translateCondCode(CC, SHCC, Swap, Invert))
    return SDValue();

  if (Swap)
    std::swap(LHS, RHS);

  // Encode: cc that BT should use (or BF if Invert). The BRCC pseudo encodes
  // the condition as (SHCC | (Invert ? 0x80 : 0)).
  unsigned EncodedCC = (unsigned)SHCC | (Invert ? 0x80 : 0);

  return DAG.getNode(SHISD::BRCC, DL, MVT::Other, Chain, Dest, LHS, RHS,
                     DAG.getTargetConstant(EncodedCC, DL, MVT::i32));
}

SDValue SHTargetLowering::LowerBRCOND(SDValue Op, SelectionDAG &DAG) const {
  // BRCOND operands: Chain, Cond, Dest
  // SH's BT/BF branch on the T-bit, not on a GPR value.
  // Convert BRCOND(chain, cond, dest) to:
  //   CMP/EQ #0, cond  (sets T=1 if cond==0)
  //   BF dest           (branch if T==0, i.e. cond!=0)
  // This is equivalent to: if (cond != 0) goto dest.
  SDValue Chain = Op.getOperand(0);
  SDValue Cond = Op.getOperand(1);
  SDValue Dest = Op.getOperand(2);
  SDLoc DL(Op);

  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  // NE(cond, 0) → Invert=true, CC=EQ → uses CMP/EQ + BF
  unsigned EncodedCC = (unsigned)SHCC::EQ | 0x80; // NE = EQ inverted

  return DAG.getNode(SHISD::BRCC, DL, MVT::Other, Chain, Dest, Cond, Zero,
                     DAG.getTargetConstant(EncodedCC, DL, MVT::i32));
}

SDValue SHTargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDLoc DL(Op);

  bool Swap, Invert;
  SHCC::CondCode SHCC;
  if (!translateCondCode(CC, SHCC, Swap, Invert))
    return SDValue();
  if (Swap)
    std::swap(LHS, RHS);

  SDValue Cmp =
      DAG.getNode(SHISD::CMP, DL, MVT::Glue, LHS, RHS,
                  DAG.getTargetConstant((unsigned)SHCC, DL, MVT::i32));

  SDValue TVal = DAG.getNode(SHISD::MOVT, DL, MVT::i32, Cmp);

  if (Invert) {
    TVal = DAG.getNode(ISD::XOR, DL, MVT::i32, TVal,
                       DAG.getConstant(1, DL, MVT::i32));
  }

  return TVal;
}

SDValue SHTargetLowering::LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  // Lower SELECT_CC into a conditional branch sequence.
  // Use ISD::BRCOND expansion (let LLVM generate branches).
  return SDValue();
}

SDValue SHTargetLowering::LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setFrameAddressIsTaken(true);
  SDLoc DL(Op);
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  if (Depth != 0) {
    // Walk up the frame chain — not supported in initial implementation.
    return SDValue();
  }
  Register FrameReg =
      MF.getSubtarget<SHSubtarget>().getRegisterInfo()->getFrameRegister(MF);
  return DAG.getCopyFromReg(DAG.getEntryNode(), DL, FrameReg, MVT::i32);
}

SDValue SHTargetLowering::LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);
  SDLoc DL(Op);
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  if (Depth != 0)
    return SDValue(); // Not supported
  // The return address is in PR.
  return DAG.getCopyFromReg(DAG.getEntryNode(), DL, SH::PR, MVT::i32);
}

SDValue SHTargetLowering::LowerFrameIndex(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  int FI = cast<FrameIndexSDNode>(Op)->getIndex();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue TFI = DAG.getTargetFrameIndex(FI, PtrVT);
  return DAG.getNode(ISD::ADD, DL, PtrVT, TFI, DAG.getConstant(0, DL, PtrVT));
}

SDValue SHTargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto *SHFI = MF.getInfo<SHMachineFunctionInfo>();
  SDLoc DL(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  SDValue Chain = Op.getOperand(0);
  SDValue VAListPtr = Op.getOperand(1);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();

  // GCC SH va_list is a struct with 5 pointer fields:
  //   [0]  __va_next_o       — next GPR save area pointer
  //   [4]  __va_next_o_limit — end of GPR save area
  //   [8]  __va_next_fp      — next FP save area pointer
  //   [12] __va_next_fp_limit — end of FP save area
  //   [16] __va_next_stack   — stack overflow pointer

  // GPR save area: starts at first vararg GPR slot
  int GPRFI = SHFI->getVarArgsFrameIndex();
  unsigned NumFixedGPRs = SHFI->getNumFixedGPRArgs();
  SDValue GPRBase = DAG.getFrameIndex(GPRFI, PtrVT);
  // next_o starts past the fixed GPR args
  SDValue NextO = DAG.getNode(ISD::ADD, DL, PtrVT, GPRBase,
                              DAG.getConstant(NumFixedGPRs * 4, DL, PtrVT));
  // next_o_limit = end of GPR save area (4 regs × 4 bytes = 16 bytes)
  SDValue NextOLimit = DAG.getNode(ISD::ADD, DL, PtrVT, GPRBase,
                                   DAG.getConstant(4 * 4, DL, PtrVT));

  // FPR save area: 4 double pairs × 8 bytes = 32 bytes
  int FPRFI = SHFI->getVarArgsFPRSaveAreaFI();
  unsigned NumFixedFPRs = SHFI->getNumFixedFPRArgs();
  SDValue FPRBase = DAG.getFrameIndex(FPRFI, PtrVT);
  // next_fp starts past the fixed FPR args
  SDValue NextFP = DAG.getNode(ISD::ADD, DL, PtrVT, FPRBase,
                               DAG.getConstant(NumFixedFPRs * 8, DL, PtrVT));
  // next_fp_limit = end of FPR save area (4 pairs × 8 bytes = 32 bytes)
  SDValue NextFPLimit = DAG.getNode(ISD::ADD, DL, PtrVT, FPRBase,
                                    DAG.getConstant(4 * 8, DL, PtrVT));

  // Stack overflow area: points to caller stack args area
  int StackFI = SHFI->getVarArgsStackArgsFI();
  SDValue NextStack = DAG.getFrameIndex(StackFI, PtrVT);

  // Store all 5 fields into the va_list struct.
  SmallVector<SDValue, 6> Stores;
  auto StoreField = [&](SDValue Val, unsigned Offset) {
    SDValue Ptr = DAG.getNode(ISD::ADD, DL, PtrVT, VAListPtr,
                              DAG.getConstant(Offset, DL, PtrVT));
    Stores.push_back(
        DAG.getStore(Chain, DL, Val, Ptr, MachinePointerInfo(SV, Offset)));
  };

  StoreField(NextO, 0);
  StoreField(NextOLimit, 4);
  StoreField(NextFP, 8);
  StoreField(NextFPLimit, 12);
  StoreField(NextStack, 16);

  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
}

SDValue SHTargetLowering::LowerVACOPY(SDValue Op, SelectionDAG &DAG) const {
  // SH va_list is a 20-byte struct (5 × void*), so va_copy must copy all
  // 20 bytes.  The default "Expand" only copies a single pointer (4 bytes).
  SDLoc DL(Op);
  const Value *DestSV = cast<SrcValueSDNode>(Op.getOperand(3))->getValue();
  const Value *SrcSV = cast<SrcValueSDNode>(Op.getOperand(4))->getValue();

  return DAG.getMemcpy(Op.getOperand(0), DL,              // Chain
                       Op.getOperand(1),                  // Dest
                       Op.getOperand(2),                  // Src
                       DAG.getConstant(20, DL, MVT::i32), // Size: 5 × 4 bytes
                       Align(4),                          // Alignment
                       false,                             // isVolatile
                       false,                             // AlwaysInline
                       /*CI=*/nullptr, std::nullopt, MachinePointerInfo(DestSV),
                       MachinePointerInfo(SrcSV));
}

//===----------------------------------------------------------------------===//
// Custom Calling Convention with Independent GPR/FPR Allocation
//===----------------------------------------------------------------------===//

/// SH ABI: GPR (R4-R7) and FPR (FR4-FR11 / DR4-DR10) allocation are
/// INDEPENDENT — float args consume FPR slots but NOT GPR slots, and
/// integer args consume GPR slots but NOT FPR slots.
/// DR and FR registers overlap: DR4 = FR4+FR5, DR6 = FR6+FR7, etc.
/// When an f32 consumes FR5 (the odd half of DR4), DR4 must not be used
/// for f64.  The tablegen CC cannot model this overlap.
template <typename ArgVec>
static void CC_SH_Custom(CCState &State, const ArgVec &Args) {
  bool FRUsed[16] = {};
  unsigned NextGPR = 0;
  static const MCPhysReg GPRs[] = {SH::R4, SH::R5, SH::R6, SH::R7};
  static const MCPhysReg FR32s[] = {SH::FR5, SH::FR4, SH::FR7,  SH::FR6,
                                    SH::FR9, SH::FR8, SH::FR11, SH::FR10};
  unsigned NextFR32 = 0;
  static const unsigned DRBases[] = {4, 6, 8, 10};
  static const MCPhysReg DRRegs[] = {SH::DR4, SH::DR6, SH::DR8, SH::DR10};
  unsigned NextDR = 0;

  auto getFRIndex = [](MCPhysReg FR) -> unsigned {
    switch (FR) {
    case SH::FR4:
      return 4;
    case SH::FR5:
      return 5;
    case SH::FR6:
      return 6;
    case SH::FR7:
      return 7;
    case SH::FR8:
      return 8;
    case SH::FR9:
      return 9;
    case SH::FR10:
      return 10;
    case SH::FR11:
      return 11;
    default:
      return 0;
    }
  };

  bool SplitStartOnStack =
      false; // Tracks if a split i64's first half was pushed to stack
  for (unsigned I = 0, E = Args.size(); I != E; ++I) {
    MVT VT = Args[I].VT;
    MVT OrigVT = Args[I].VT;
    ISD::ArgFlagsTy Flags = Args[I].Flags;

    if (VT == MVT::i8 || VT == MVT::i16)
      VT = MVT::i32;

    if (Flags.isByVal()) {
      unsigned Size = Flags.getByValSize();
      unsigned NumRegs = (Size + 3) / 4;
      if (NextGPR + NumRegs <= 4) {
        // Fits entirely in GPRs.
        State.addLoc(CCValAssign::getReg(I, OrigVT, GPRs[NextGPR], MVT::i32,
                                         CCValAssign::Full));
        NextGPR += NumRegs;
      } else {
        // Doesn't fit in remaining GPRs. Pass entirely on the stack.
        unsigned Off = State.AllocateStack(Size, Align(4));
        State.addLoc(
            CCValAssign::getMem(I, OrigVT, Off, MVT::i32, CCValAssign::Full));
      }
      continue;
    }

    if (VT == MVT::f32) {
      bool Assigned = false;
      while (NextFR32 < 8) {
        MCPhysReg FR = FR32s[NextFR32];
        unsigned Idx = getFRIndex(FR);
        NextFR32++;
        if (!FRUsed[Idx]) {
          FRUsed[Idx] = true;
          State.addLoc(
              CCValAssign::getReg(I, OrigVT, FR, MVT::f32, CCValAssign::Full));
          Assigned = true;
          break;
        }
      }
      if (!Assigned) {
        unsigned Off = State.AllocateStack(4, Align(4));
        State.addLoc(
            CCValAssign::getMem(I, OrigVT, Off, MVT::f32, CCValAssign::Full));
      }
    } else if (VT == MVT::f64) {
      bool Assigned = false;
      while (NextDR < 4) {
        unsigned Base = DRBases[NextDR];
        MCPhysReg DR = DRRegs[NextDR];
        NextDR++;
        if (!FRUsed[Base] && !FRUsed[Base + 1]) {
          FRUsed[Base] = true;
          FRUsed[Base + 1] = true;
          State.addLoc(
              CCValAssign::getReg(I, OrigVT, DR, MVT::f64, CCValAssign::Full));
          Assigned = true;
          break;
        }
      }
      if (!Assigned) {
        unsigned Off = State.AllocateStack(8, Align(4));
        State.addLoc(
            CCValAssign::getMem(I, OrigVT, Off, MVT::f64, CCValAssign::Full));
      }
    } else {
      // i32 (or promoted i8/i16) — allocate from GPR list
      // Special case: when this is the first half of an i64 (isSplit()),
      // GCC's SH ABI puts the entire i64 on the stack if fewer than 2
      // GPRs remain, keeping the last GPR for subsequent non-split args.
      //
      // GCC SH ABI: the first 4 integer/pointer args always go in R4-R7,
      // even for variadic calls. The callee saves R4-R7 to a GPR save
      // area and the va_list struct references that area.
      bool IsSplitStart = Flags.isSplit();
      bool IsSplitEnd = Flags.isSplitEnd();

      if (IsSplitStart && NextGPR >= 3) {
        // Only 1 (or 0) GPR left — can't fit both halves.
        // Push this half to stack; flag the next SplitEnd.
        SplitStartOnStack = true;
        unsigned Off = State.AllocateStack(4, Align(4));
        State.addLoc(
            CCValAssign::getMem(I, OrigVT, Off, MVT::i32, CCValAssign::Full));
      } else if (IsSplitEnd && SplitStartOnStack) {
        // Second half follows first half to stack.
        SplitStartOnStack = false;
        unsigned Off = State.AllocateStack(4, Align(4));
        State.addLoc(
            CCValAssign::getMem(I, OrigVT, Off, MVT::i32, CCValAssign::Full));
      } else if (NextGPR < 4 && (Flags.isInReg() || !State.isVarArg())) {
        // For varargs calls, only use GPRs for args with InReg flag.
        // Non-InReg args in varargs are struct components that don't fit
        // entirely in remaining GPRs and must go to the stack to avoid
        // splitting across GPR save area and stack (va_arg reads contiguously).
        CCValAssign::LocInfo LI = CCValAssign::Full;
        if (OrigVT == MVT::i8 || OrigVT == MVT::i16)
          LI = CCValAssign::SExt;
        State.addLoc(
            CCValAssign::getReg(I, OrigVT, GPRs[NextGPR], MVT::i32, LI));
        NextGPR++;
      } else {
        unsigned Off = State.AllocateStack(4, Align(4));
        State.addLoc(
            CCValAssign::getMem(I, OrigVT, Off, MVT::i32, CCValAssign::Full));
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Formal Arguments
//===----------------------------------------------------------------------===//

SDValue SHTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *SHFI = MF.getInfo<SHMachineFunctionInfo>();

  // Assign locations to all formal arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CC_SH_Custom(CCInfo, Ins);

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    ISD::ArgFlagsTy Flags = Ins[I].Flags;

    if (Flags.isByVal()) {
      unsigned ByValSize = Flags.getByValSize();
      int FI;
      if (VA.isMemLoc()) {
        FI = MFI.CreateFixedObject(ByValSize, VA.getLocMemOffset(), true);
        SDValue FIPtr =
            DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
        InVals.push_back(
            FIPtr); // For byval on stack, the argument is the pointer
      } else {
        if (!VA.isRegLoc())
          report_fatal_error(
              "LowerFormalArguments: isByVal but not RegLoc or MemLoc");
        // ByVal passed as a pointer in a register
        FI = MFI.CreateStackObject(ByValSize, Flags.getNonZeroByValAlign(),
                                   false);
        SDValue FIPtr =
            DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));

        const TargetRegisterClass *RC = &SH::GPRRegClass;
        Register VReg = MF.getRegInfo().createVirtualRegister(RC);
        MF.getRegInfo().addLiveIn(VA.getLocReg(), VReg);
        SDValue ArgPtr = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());

        // Memcpy the struct from the passed pointer to the local stack frame
        SDValue SizeNode = DAG.getConstant(ByValSize, DL, MVT::i32);
        Chain = DAG.getMemcpy(
            Chain, DL, FIPtr, ArgPtr, SizeNode, Flags.getNonZeroByValAlign(),
            /*isVolatile=*/false, /*AlwaysInline=*/false,
            /*CI=*/nullptr,
            /*isTailCall=*/false, MachinePointerInfo(), MachinePointerInfo());
        InVals.push_back(FIPtr);
      }
      continue;
    } else if (VA.isRegLoc()) {
      // Argument is passed in a register.
      const TargetRegisterClass *RC;
      if (VA.getLocVT() == MVT::f32)
        RC = &SH::FPR32RegClass;
      else if (VA.getLocVT() == MVT::f64)
        RC = &SH::FPR64RegClass;
      else
        RC = &SH::GPRRegClass;

      Register VReg = MF.getRegInfo().createVirtualRegister(RC);
      MF.getRegInfo().addLiveIn(VA.getLocReg(), VReg);
      SDValue ArgVal = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());

      if (VA.getLocInfo() == CCValAssign::SExt)
        ArgVal = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), ArgVal,
                             DAG.getValueType(VA.getValVT()));
      else if (VA.getLocInfo() == CCValAssign::ZExt)
        ArgVal = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), ArgVal,
                             DAG.getValueType(VA.getValVT()));

      if (VA.getLocInfo() != CCValAssign::Full)
        ArgVal = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), ArgVal);

      InVals.push_back(ArgVal);
    } else {
      // Argument is on the stack.
      if (!VA.isMemLoc())
        report_fatal_error("LowerFormalArguments: arg not MemLoc or RegLoc "
                           "(Unexpected CCValAssign)");
      // Stack args from the caller start at SP+0.
      unsigned MemOff = VA.getLocMemOffset();
      int FI = MFI.CreateFixedObject(VA.getValVT().getSizeInBits() / 8, MemOff,
                                     true);
      SDValue FIPtr = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      SDValue Load = DAG.getLoad(VA.getValVT(), DL, Chain, FIPtr,
                                 MachinePointerInfo::getFixedStack(MF, FI));
      InVals.push_back(Load);
    }
  }

  if (isVarArg) {
    // GCC SH ABI: variadic functions must save both GPR (R4-R7) and
    // FPR (DR4, DR6, DR8, DR10 = FR4:FR5, FR6:FR7, FR8:FR9, FR10:FR11)
    // register arguments into separate save areas. The va_list struct
    // has separate pointers into each area, plus a stack overflow ptr.

    static const MCPhysReg ArgGPRs[] = {SH::R4, SH::R5, SH::R6, SH::R7};
    static const MCPhysReg ArgFPRHi[] = {SH::FR4, SH::FR6, SH::FR8, SH::FR10};
    static const MCPhysReg ArgFPRLo[] = {SH::FR5, SH::FR7, SH::FR9, SH::FR11};

    // Count how many fixed args consumed GPR and FPR register slots.
    // FPR slots are DR pairs (8 bytes each). An f64 consumes one pair.
    // An f32 uses one FR register; two FRs in a pair = that pair is consumed.
    unsigned NumFixedGPRs = 0;
    bool DRPairUsed[4] = {}; // DR4, DR6, DR8, DR10
    for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
      CCValAssign &VA = ArgLocs[I];
      if (VA.isRegLoc()) {
        if (VA.getLocVT() == MVT::i32) {
          NumFixedGPRs++;
        } else if (VA.getLocVT() == MVT::f64) {
          // DR4=pair0, DR6=pair1, DR8=pair2, DR10=pair3
          MCPhysReg Reg = VA.getLocReg();
          if (Reg == SH::DR4)
            DRPairUsed[0] = true;
          else if (Reg == SH::DR6)
            DRPairUsed[1] = true;
          else if (Reg == SH::DR8)
            DRPairUsed[2] = true;
          else if (Reg == SH::DR10)
            DRPairUsed[3] = true;
        } else if (VA.getLocVT() == MVT::f32) {
          // FR4/5→pair0, FR6/7→pair1, FR8/9→pair2, FR10/11→pair3
          MCPhysReg Reg = VA.getLocReg();
          unsigned FRIdx = 0;
          switch (Reg) {
          case SH::FR4:
          case SH::FR5:
            FRIdx = 0;
            break;
          case SH::FR6:
          case SH::FR7:
            FRIdx = 1;
            break;
          case SH::FR8:
          case SH::FR9:
            FRIdx = 2;
            break;
          case SH::FR10:
          case SH::FR11:
            FRIdx = 3;
            break;
          }
          DRPairUsed[FRIdx] = true;
        }
      }
    }
    // Count contiguous consumed DR pairs from the start.
    unsigned NumFixedFPRs = 0;
    for (unsigned I = 0; I < 4; ++I) {
      if (DRPairUsed[I])
        NumFixedFPRs = I + 1;
      else
        break;
    }
    if (NumFixedGPRs > 4)
      NumFixedGPRs = 4;

    SHFI->setNumFixedGPRArgs(NumFixedGPRs);
    SHFI->setNumFixedFPRArgs(NumFixedFPRs);

    // --- GPR save area: 4 regs × 4 bytes = 16 bytes ---
    int GPRFI = MFI.CreateStackObject(16, Align(4), false);
    SHFI->setVarArgsFrameIndex(GPRFI);
    SDValue GPRBase =
        DAG.getFrameIndex(GPRFI, getPointerTy(DAG.getDataLayout()));

    // Save ALL 4 GPR args (even fixed ones, for simplicity & correctness).
    for (unsigned I = 0; I < 4; ++I) {
      Register VReg = MF.getRegInfo().createVirtualRegister(&SH::GPRRegClass);
      MF.getRegInfo().addLiveIn(ArgGPRs[I], VReg);
      SDValue ArgVal = DAG.getCopyFromReg(Chain, DL, VReg, MVT::i32);
      SDValue SlotAddr = DAG.getNode(ISD::ADD, DL, MVT::i32, GPRBase,
                                     DAG.getIntPtrConstant(I * 4, DL));
      Chain = DAG.getStore(Chain, DL, ArgVal, SlotAddr, MachinePointerInfo());
    }

    // --- FPR save area: 4 double pairs × 8 bytes = 32 bytes ---
    // SH DRn = (FRn=MSW, FRn+1=LSW) in little-endian
    // Store as two fmov.s with LE ordering: LSW at lower addr, MSW at higher.
    int FPRFI = MFI.CreateStackObject(32, Align(8), false);
    SHFI->setVarArgsFPRSaveAreaFI(FPRFI);
    SDValue FPRBase =
        DAG.getFrameIndex(FPRFI, getPointerTy(DAG.getDataLayout()));

    for (unsigned I = 0; I < 4; ++I) {
      // Save FRn+1 (LSW) at [base + I*8], FRn (MSW) at [base + I*8 + 4]
      Register VRegLo =
          MF.getRegInfo().createVirtualRegister(&SH::FPR32RegClass);
      Register VRegHi =
          MF.getRegInfo().createVirtualRegister(&SH::FPR32RegClass);
      MF.getRegInfo().addLiveIn(ArgFPRLo[I], VRegLo);
      MF.getRegInfo().addLiveIn(ArgFPRHi[I], VRegHi);
      SDValue ValLo = DAG.getCopyFromReg(Chain, DL, VRegLo, MVT::f32);
      SDValue ValHi = DAG.getCopyFromReg(Chain, DL, VRegHi, MVT::f32);

      SDValue LoAddr = DAG.getNode(ISD::ADD, DL, MVT::i32, FPRBase,
                                   DAG.getIntPtrConstant(I * 8, DL));
      SDValue HiAddr = DAG.getNode(ISD::ADD, DL, MVT::i32, FPRBase,
                                   DAG.getIntPtrConstant(I * 8 + 4, DL));
      Chain = DAG.getStore(Chain, DL, ValLo, LoAddr, MachinePointerInfo());
      Chain = DAG.getStore(Chain, DL, ValHi, HiAddr, MachinePointerInfo());
    }

    // --- Stack overflow area: points to caller's VARIADIC stack args ---
    // Fixed args that spilled to the stack occupy the first N slots.
    // Count how many fixed args are on the stack so we skip past them.
    unsigned FixedStackBytes = 0;
    for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
      CCValAssign &VA = ArgLocs[I];
      if (VA.isMemLoc()) {
        unsigned ArgSize = VA.getLocVT().getSizeInBits() / 8;
        if (ArgSize < 4)
          ArgSize = 4; // minimum 4-byte slot
        unsigned EndOffset = VA.getLocMemOffset() + ArgSize;
        if (EndOffset > FixedStackBytes)
          FixedStackBytes = EndOffset;
      }
    }
    int StackFI = MFI.CreateFixedObject(4, FixedStackBytes, true);
    SHFI->setVarArgsStackArgsFI(StackFI);
  }

  // PR is implicitly used by RTS, so it must be live-in to the function
  // even if it's a leaf function that doesn't save/restore it.
  if (!MF.getRegInfo().isLiveIn(SH::PR))
    MF.getRegInfo().addLiveIn(SH::PR);

  return Chain;
}

#include "llvm/Support/raw_ostream.h"

//===----------------------------------------------------------------------===//
// Call lowering
//===----------------------------------------------------------------------===//

SDValue SHTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                    SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  auto &Outs = CLI.Outs;
  auto &OutVals = CLI.OutVals;
  auto &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool isVarArg = CLI.IsVarArg;
  MachineFunction &MF = DAG.getMachineFunction();

  // SH does not currently support tail calls.
  CLI.IsTailCall = false;

  // Compute argument layout using custom CC with proper FPR overlap tracking.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CC_SH_Custom(CCInfo, Outs);

  unsigned NumBytes = CCInfo.getStackSize();

  // For vararg calls, the caller must reserve an additional 16 bytes (4 GPR
  // arg slots) so the callee can spill register-passed variadic args into a
  // contiguous area adjacent to any stack-passed args.
  if (isVarArg)
    NumBytes += 16;

  // Adjust SP down.
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  SmallVector<std::tuple<unsigned, SDValue, MVT>, 4> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    SDValue Arg = OutVals[I];
    ISD::ArgFlagsTy Flags = Outs[I].Flags;

    if (Flags.isByVal()) {
      unsigned ByValSize = Flags.getByValSize();
      if (VA.isRegLoc()) {
        // Fits entirely in GPRs - pass as pointer for memcpy in callee.
        RegsToPass.emplace_back(VA.getLocReg(), Arg, VA.getLocVT());
      } else {
        if (!VA.isMemLoc())
          report_fatal_error("LowerCall: isByVal but not MemLoc or RegLoc");
        SDValue StackPtr = DAG.getCopyFromReg(Chain, DL, SH::R15, MVT::i32);
        SDValue PtrOff = DAG.getIntPtrConstant(VA.getLocMemOffset(), DL);
        SDValue Addr = DAG.getNode(ISD::ADD, DL, MVT::i32, StackPtr, PtrOff);

        SDValue SizeNode = DAG.getConstant(ByValSize, DL, MVT::i32);
        Chain = DAG.getMemcpy(
            Chain, DL, Addr, Arg, SizeNode, Flags.getNonZeroByValAlign(),
            /*isVolatile=*/false, /*AlwaysInline=*/false,
            /*CI=*/nullptr,
            /*isTailCall=*/false, MachinePointerInfo(), MachinePointerInfo());
        MemOpChains.push_back(Chain);
      }
      continue;
    }

    if (VA.isRegLoc()) {
      RegsToPass.emplace_back(VA.getLocReg(), Arg, VA.getLocVT());
    } else {
      if (!VA.isMemLoc())
        report_fatal_error("LowerCall: Arg is neither RegLoc nor MemLoc");
      SDValue StackPtr = DAG.getCopyFromReg(Chain, DL, SH::R15, MVT::i32);
      unsigned MemOffset = VA.getLocMemOffset();
      SDValue PtrOff = DAG.getIntPtrConstant(MemOffset, DL);
      SDValue Addr = DAG.getNode(ISD::ADD, DL, MVT::i32, StackPtr, PtrOff);

      SDValue Store = DAG.getStore(Chain, DL, Arg, Addr, MachinePointerInfo());
      MemOpChains.push_back(Store);
    }
  }

  // Flush memory stores.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  // Build register argument copies.
  SDValue Glue;
  for (auto &Reg : RegsToPass) {
    Chain =
        DAG.getCopyToReg(Chain, DL, std::get<0>(Reg), std::get<1>(Reg), Glue);
    Glue = Chain.getValue(1);
  }

  if (isa<GlobalAddressSDNode>(Callee)) {
    Callee = LowerGlobalAddress(Callee, DAG);
  } else if (isa<ExternalSymbolSDNode>(Callee)) {
    Callee = LowerExternalSymbol(Callee, DAG);
  }

  // Build the call node.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);
  for (auto &Reg : RegsToPass)
    Ops.push_back(DAG.getRegister(std::get<0>(Reg), std::get<2>(Reg)));

  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (Glue.getNode())
    Ops.push_back(Glue);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  unsigned CallOpc = SHISD::CALLR;
  Chain = DAG.getNode(CallOpc, DL, NodeTys, Ops);
  Glue = Chain.getValue(1);

  // Restore SP.
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);

  // Copy return values.
  SmallVector<CCValAssign, 4> RVLocs;
  CCState RetInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  RetInfo.AnalyzeCallResult(Ins, RetCC_SH);

  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    CCValAssign &VA = RVLocs[I];
    if (!VA.isRegLoc())
      report_fatal_error("LowerCall: Return value not in RegLoc!");
    SDValue RetValue =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue);
    Chain = RetValue.getValue(1);
    Glue = RetValue.getValue(2);

    if (VA.getLocInfo() == CCValAssign::SExt)
      RetValue = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), RetValue,
                             DAG.getValueType(VA.getValVT()));
    else if (VA.getLocInfo() == CCValAssign::ZExt)
      RetValue = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), RetValue,
                             DAG.getValueType(VA.getValVT()));

    if (VA.getLocInfo() != CCValAssign::Full)
      RetValue = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), RetValue);

    InVals.push_back(RetValue);
  }

  return Chain;
}

//===----------------------------------------------------------------------===//
// Return lowering
//===----------------------------------------------------------------------===//

bool SHTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_SH);
}

SDValue
SHTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                              bool isVarArg,
                              const SmallVectorImpl<ISD::OutputArg> &Outs,
                              const SmallVectorImpl<SDValue> &OutVals,
                              const SDLoc &DL, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  // Assign locations to return values.
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_SH);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps{Chain};

  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    CCValAssign &VA = RVLocs[I];
    if (!VA.isRegLoc())
      report_fatal_error("LowerReturn: Return value must be in a register");
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[I], Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(SHISD::RET, DL, MVT::Other, RetOps);
}

TargetLowering::ConstraintType
SHTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'r':
      return C_RegisterClass;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
SHTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                               StringRef Constraint,
                                               MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      return std::make_pair(0U, &SH::GPRRegClass);
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

Register SHTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  return SH::R4;
}

Register SHTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  return SH::R5;
}

SDValue SHTargetLowering::LowerEH_RETURN(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Offset = Op.getOperand(1);
  SDValue Handler = Op.getOperand(2);
  SDLoc DL(Op);

  // According to standard SH Exception Handling:
  // We place the exception return offset into an agreed register (e.g., R0),
  // and the target block address into another register (e.g., R1),
  // then let custom code build the stack unwinding. Wait.
  // We can just rely on standard definitions:
  SDValue TargetR12 = DAG.getCopyToReg(Chain, DL, SH::R12, Handler);
  SDValue TargetR13 = DAG.getCopyToReg(TargetR12, DL, SH::R13, Offset);

  return DAG.getNode(SHISD::EH_RETURN, DL, MVT::Other, TargetR13,
                     DAG.getRegister(SH::R12, MVT::i32),
                     DAG.getRegister(SH::R13, MVT::i32));
}

/// Lower SREM/UREM to: a - (a / b) * b
/// SH libgcc does not provide __modsi3/__umodsi3, but it does provide
/// __sdivsi3/__udivsi3. We implement remainder using those.
SDValue SHTargetLowering::LowerREM(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);

  // Compute quotient = a / b (will be expanded to __sdivsi3 or __udivsi3)
  unsigned DivOpc = (Op.getOpcode() == ISD::SREM) ? ISD::SDIV : ISD::UDIV;
  SDValue Div = DAG.getNode(DivOpc, DL, VT, LHS, RHS);

  // Compute remainder = a - (quotient * b)
  SDValue Mul = DAG.getNode(ISD::MUL, DL, VT, Div, RHS);
  SDValue Rem = DAG.getNode(ISD::SUB, DL, VT, LHS, Mul);

  return Rem;
}

MachineBasicBlock *
SHTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                              MachineBasicBlock *BB) const {
  unsigned Opc = MI.getOpcode();

  if (Opc != SH::Select_i32 && Opc != SH::Select_f32 && Opc != SH::Select_f64)
    return BB;

  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  MachineFunction *MF = BB->getParent();
  MachineBasicBlock *IfFalseBB =
      MF->CreateMachineBasicBlock(BB->getBasicBlock());
  MachineBasicBlock *EndBB = MF->CreateMachineBasicBlock(BB->getBasicBlock());

  MF->insert(++BB->getIterator(), IfFalseBB);
  MF->insert(++IfFalseBB->getIterator(), EndBB);

  // Transfer the remainder of BB and its successor edges to EndBB.
  EndBB->splice(EndBB->begin(), BB, std::next(MachineBasicBlock::iterator(MI)),
                BB->end());
  EndBB->transferSuccessorsAndUpdatePHIs(BB);

  BB->addSuccessor(IfFalseBB);
  BB->addSuccessor(EndBB);

  // tst Cond, Cond
  // bt IfFalseBB
  BuildMI(BB, DL, TII->get(SH::TST_RR))
      .addReg(MI.getOperand(3).getReg())
      .addReg(MI.getOperand(3).getReg());
  BuildMI(BB, DL, TII->get(SH::BT)).addMBB(IfFalseBB);
  // Fallthrough to EndBB is not topologically possible since IfFalseBB is next,
  // so we must put an unconditional branch.
  BuildMI(BB, DL, TII->get(SH::BRA)).addMBB(EndBB);

  IfFalseBB->addSuccessor(EndBB);

  BuildMI(*EndBB, EndBB->begin(), DL, TII->get(SH::PHI),
          MI.getOperand(0).getReg())
      .addReg(MI.getOperand(1).getReg())
      .addMBB(BB)
      .addReg(MI.getOperand(2).getReg())
      .addMBB(IfFalseBB);

  MI.eraseFromParent();
  return EndBB;
}

bool SHTargetLowering::allowsMisalignedMemoryAccesses(
    EVT VT, unsigned AddrSpace, Align Alignment, MachineMemOperand::Flags Flags,
    unsigned *Fast) const {
  // SH requires strict alignment for all memory accesses:
  //   - i8/byte: 1-byte aligned (no requirement)
  //   - i16/word: 2-byte aligned
  //   - i32/longword/f32: 4-byte aligned
  //   - f64/double: 4-byte aligned (pair of 32-bit accesses)
  // Misaligned accesses cause a SIGBUS (address error exception).
  if (Fast)
    *Fast = 0;
  return false;
}
