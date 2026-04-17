//===-- SHConstantPoolValue.h - SH constant pool value --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SH-specific constant pool values.  These wrap globals, external symbols,
// block addresses, and jump table indices so they can live in the standard
// MachineConstantPool and be placed by the Constant Island pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SH_SHCONSTANTPOOLVALUE_H
#define LLVM_LIB_TARGET_SH_SHCONSTANTPOOLVALUE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include <string>

namespace llvm {

class BlockAddress;
class GlobalValue;
class LLVMContext;
class MachineBasicBlock;
class raw_ostream;

namespace SHCP {
enum SHCPKind {
  CPGlobalValue,
  CPExtSymbol,
  CPBlockAddress,
  CPMachineBasicBlock,
  CPJumpTable,
  CPCPIAddr
};

/// Relocation modifier for constant pool entries.
enum SHCPModifier {
  no_modifier, /// Plain address
  GOT,         /// @GOT  (PIC global via GOT)
  GOTPC,       /// @GOTPC (_GLOBAL_OFFSET_TABLE_)
  TPOFF,       /// @TPOFF (TLS thread-pointer offset)
};
} // end namespace SHCP

/// SHConstantPoolValue - Base class for SH-specific constant pool entries.
class SHConstantPoolValue : public MachineConstantPoolValue {
  SHCP::SHCPKind Kind;

protected:
  SHConstantPoolValue(Type *Ty, SHCP::SHCPKind Kind);
  SHConstantPoolValue(LLVMContext &C, SHCP::SHCPKind Kind);

public:
  ~SHConstantPoolValue() override;

  SHCP::SHCPKind getKind() const { return Kind; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;
};

/// SHConstantPoolGV - Wraps a GlobalValue for the constant pool.
class SHConstantPoolGV : public SHConstantPoolValue {
  const GlobalValue *GV;
  SHCP::SHCPModifier Modifier;
  SHConstantPoolGV(const GlobalValue *GV, SHCP::SHCPModifier Mod);

public:
  static SHConstantPoolGV *Create(const GlobalValue *GV,
                                  SHCP::SHCPModifier Mod = SHCP::no_modifier);
  const GlobalValue *getGlobalValue() const { return GV; }
  SHCP::SHCPModifier getModifier() const { return Modifier; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  static bool classof(const SHConstantPoolValue *V) {
    return V->getKind() == SHCP::CPGlobalValue;
  }
};

/// SHConstantPoolSymbol - Wraps an external symbol name.
class SHConstantPoolSymbol : public SHConstantPoolValue {
  std::string Symbol;
  SHCP::SHCPModifier Modifier;
  SHConstantPoolSymbol(LLVMContext &C, StringRef S, SHCP::SHCPModifier Mod);

public:
  static SHConstantPoolSymbol *
  Create(LLVMContext &C, StringRef S,
         SHCP::SHCPModifier Mod = SHCP::no_modifier);
  StringRef getSymbol() const { return Symbol; }
  SHCP::SHCPModifier getModifier() const { return Modifier; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  static bool classof(const SHConstantPoolValue *V) {
    return V->getKind() == SHCP::CPExtSymbol;
  }
};

/// SHConstantPoolBA - Wraps a BlockAddress.
class SHConstantPoolBA : public SHConstantPoolValue {
  const BlockAddress *BA;
  int Offset;
  SHConstantPoolBA(const BlockAddress *BA, int Offset);

public:
  static SHConstantPoolBA *Create(const BlockAddress *BA, int Offset = 0);
  const BlockAddress *getBlockAddress() const { return BA; }
  int getOffset() const { return Offset; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  static bool classof(const SHConstantPoolValue *V) {
    return V->getKind() == SHCP::CPBlockAddress;
  }
};

/// SHConstantPoolMBB - Wraps a MachineBasicBlock.
class SHConstantPoolMBB : public SHConstantPoolValue {
  const MachineBasicBlock *MBB;
  SHConstantPoolMBB(LLVMContext &C, const MachineBasicBlock *MBB);

public:
  static SHConstantPoolMBB *Create(LLVMContext &C,
                                   const MachineBasicBlock *MBB);
  const MachineBasicBlock *getMBB() const { return MBB; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  static bool classof(const SHConstantPoolValue *V) {
    return V->getKind() == SHCP::CPMachineBasicBlock;
  }
};

/// SHConstantPoolJTI - Wraps a jump table index.
class SHConstantPoolJTI : public SHConstantPoolValue {
  unsigned JTIdx;
  SHConstantPoolJTI(LLVMContext &C, unsigned Idx);

public:
  static SHConstantPoolJTI *Create(LLVMContext &C, unsigned Idx);
  unsigned getJumpTableIndex() const { return JTIdx; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  static bool classof(const SHConstantPoolValue *V) {
    return V->getKind() == SHCP::CPJumpTable;
  }
};

/// SHConstantPoolCPIAddr - Wraps a CPI index to emit its address.
/// Emits '.long .LCPI_N' — the address of a constant pool entry.
/// Used for FP constant materialization where we need the address
/// of the CPI data to load via FMOV.S @Rn, FRn.
class SHConstantPoolCPIAddr : public SHConstantPoolValue {
  unsigned CPIdx;
  SHConstantPoolCPIAddr(LLVMContext &C, unsigned Idx);

public:
  static SHConstantPoolCPIAddr *Create(LLVMContext &C, unsigned Idx);
  unsigned getCPIndex() const { return CPIdx; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  static bool classof(const SHConstantPoolValue *V) {
    return V->getKind() == SHCP::CPCPIAddr;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SH_SHCONSTANTPOOLVALUE_H
