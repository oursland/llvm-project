//===-- SHConstantIslandPass.cpp - Emit PC Relative loads ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass is used to make PC-relative constant pool loads work.
// SH has very short PC-relative displacements (8-bit scaled by 4 for MOVL,
// scaled by 2 for MOVW). This means constant pools must be inserted
// relatively close to the loads that reference them.
//
//===----------------------------------------------------------------------===//

#include "SH.h"
#include "SHConstantPoolValue.h"
#include "SHInstrInfo.h"
#include "SHSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "sh-cp-islands"

STATISTIC(NumCPEs, "Number of constpool entries");
STATISTIC(NumSplit, "Number of uncond branches inserted");

namespace {

class SHConstantIslands : public MachineFunctionPass {
  struct BasicBlockInfo {
    unsigned Offset = 0;
    unsigned Size = 0;

    /// KnownBits - The number of low bits in Offset that are known to be
    /// exact.  The remaining bits of Offset are an upper bound.
    uint8_t KnownBits = 0;

    /// PostAlign - When > 1, the block terminator contains a .align
    /// directive, so the end of the block is aligned to PostAlign bytes.
    Align PostAlign;

    BasicBlockInfo() = default;

    /// Compute the number of known offset bits internally to this block.
    unsigned internalKnownBits() const {
      unsigned Bits = KnownBits;
      // If the block size isn't a multiple of the known bits, assume the
      // worst case padding.
      if (Size & ((1u << Bits) - 1))
        Bits = llvm::countr_zero(Size);
      return Bits;
    }

    /// Compute the offset immediately following this block.  If Alignment
    /// is specified, include worst-case padding for a successor with that
    /// alignment.
    unsigned postOffset(Align A = Align(1)) const {
      unsigned PO = Offset + Size;
      const Align PA = std::max(PostAlign, A);
      if (PA == Align(1))
        return PO;
      // Use exact alignment.  SH CI pass starts from offset 0 with
      // deterministic instruction sizes, so all offsets are exact.
      return alignTo(PO, PA);
    }

    /// Compute the number of known low bits of postOffset.
    unsigned postKnownBits(Align A = Align(1)) const {
      return llvm::countr_zero(postOffset(A));
    }
  };

  std::vector<BasicBlockInfo> BBInfo;
  std::vector<MachineBasicBlock *> WaterList;
  SmallPtrSet<MachineBasicBlock *, 4> NewWaterList;

  using water_iterator = std::vector<MachineBasicBlock *>::iterator;

  struct CPUser {
    MachineInstr *MI;
    MachineInstr *CPEMI;
    MachineBasicBlock *HighWaterMark;
    unsigned MaxDisp;
    bool NegOk;
    bool KnownAlignment = false;
    unsigned CloneCount = 0;

    CPUser(MachineInstr *Mi, MachineInstr *Cpemi, unsigned Maxdisp, bool Neg)
        : MI(Mi), CPEMI(Cpemi), MaxDisp(Maxdisp), NegOk(Neg) {
      HighWaterMark = CPEMI->getParent();
    }

    /// getMaxDisp - Returns the maximum displacement supported by MI.
    /// No safety margin: the CI pass runs as the last code-layout pass
    /// (addPreEmitPass2) so nothing can change block sizes after it.
    unsigned getMaxDisp() const { return MaxDisp; }
  };

  std::vector<CPUser> CPUsers;

  struct CPEntry {
    MachineInstr *CPEMI;
    unsigned CPI;
    unsigned RefCount;

    CPEntry(MachineInstr *Cpemi, unsigned Cpi, unsigned Rc = 0)
        : CPEMI(Cpemi), CPI(Cpi), RefCount(Rc) {}
  };

  std::vector<std::vector<CPEntry>> CPEntries;

  const SHSubtarget *STI = nullptr;
  const SHInstrInfo *TII;
  MachineFunction *MF = nullptr;
  MachineConstantPool *MCP = nullptr;

  unsigned PICLabelUId;
  void initPICLabelUId(unsigned UId) { PICLabelUId = UId; }
  unsigned createPICLabelUId() { return PICLabelUId++; }

public:
  static char ID;

  SHConstantIslands() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "SH Constant Islands"; }

  bool runOnMachineFunction(MachineFunction &F) override;
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  void doInitialPlacement(std::vector<MachineInstr *> &CPEMIs);
  void doInitialJumpTablePlacement(std::vector<MachineInstr *> &CPEMIs);
  CPEntry *findConstPoolEntry(unsigned CPI, const MachineInstr *CPEMI);
  Align getCPEAlign(const MachineInstr &CPEMI);
  void initializeFunctionInfo(const std::vector<MachineInstr *> &CPEMIs);
  unsigned getOffsetOf(MachineInstr *MI) const;
  unsigned getUserOffset(CPUser &) const;

  bool isOffsetInRange(unsigned UserOffset, unsigned TrialOffset, unsigned Disp,
                       bool NegativeOK);
  bool isOffsetInRange(unsigned UserOffset, unsigned TrialOffset,
                       const CPUser &U);

  void computeBlockSize(MachineBasicBlock *MBB);
  MachineBasicBlock *splitBlockBeforeInstr(MachineInstr &MI);
  void updateForInsertedWaterBlock(MachineBasicBlock *NewBB);
  void adjustBBOffsetsAfter(MachineBasicBlock *BB);
  bool decrementCPEReferenceCount(unsigned CPI, MachineInstr *CPEMI);
  int findInRangeCPEntry(CPUser &U, unsigned UserOffset);
  bool findAvailableWater(CPUser &U, unsigned UserOffset,
                          water_iterator &WaterIter);
  void createNewWater(unsigned CPUserIndex, unsigned UserOffset,
                      MachineBasicBlock *&NewMBB);
  bool handleConstantPoolUser(unsigned CPUserIndex);
  bool materializeConstantInline(CPUser &U);
  void removeDeadCPEMI(MachineInstr *CPEMI);
  bool removeUnusedCPEntries();
  bool isCPEntryInRange(MachineInstr *MI, unsigned UserOffset,
                        MachineInstr *CPEMI, unsigned Disp, bool NegOk,
                        bool DoDump = false);
  bool isWaterInRange(unsigned UserOffset, MachineBasicBlock *Water, CPUser &U,
                      unsigned &Growth);

  bool fixupBranches();
  int64_t getBranchOffset(const MachineInstr *MI) const;
  bool addFallthroughProtection();
};
} // end anonymous namespace

char SHConstantIslands::ID = 0;

bool SHConstantIslands::runOnMachineFunction(MachineFunction &Mf) {
  MF = &Mf;
  MCP = Mf.getConstantPool();
  STI = &Mf.getSubtarget<SHSubtarget>();
  TII = STI->getInstrInfo();

  MF->getRegInfo().invalidateLiveness();
  MF->RenumberBlocks();

  bool MadeChange = false;

  std::vector<MachineInstr *> CPEMIs;
  if (!MCP->isEmpty())
    doInitialPlacement(CPEMIs);

  if (MF->getJumpTableInfo())
    doInitialJumpTablePlacement(CPEMIs);

  initPICLabelUId(CPEMIs.size());

  initializeFunctionInfo(CPEMIs);
  CPEMIs.clear();

  MadeChange |= removeUnusedCPEntries();

  unsigned TotalIters = 0;
  while (true) {
    bool CPChange = false;
    for (unsigned I = 0, E = CPUsers.size(); I != E; ++I)
      CPChange |= handleConstantPoolUser(I);

    NewWaterList.clear();

    bool BRChange = fixupBranches();

    // Ensure no code MBB physically falls through into CPI data.
    // Must run inside the loop: BRA+NOP insertion changes block sizes,
    // which may push CPI entries past the displacement limit.
    // addFallthroughProtection updates BBInfo via computeBlockSize +
    // adjustBBOffsetsAfter, so the next iteration sees correct offsets.
    bool FTChange = addFallthroughProtection();

    if (!CPChange && !BRChange && !FTChange)
      break;
    if (++TotalIters > 50)
      break;
    MadeChange = true;

    // Recompute offsets for the next iteration.
    adjustBBOffsetsAfter(&MF->front());
  }

  // Post-convergence safety net: apply two-level CPI indirection to any
  // MOV_I32 users whose CPIs are still out of range.  This handles cases
  // where the convergence loop hit its iteration limit before all CPIs
  // could be resolved.
  bool AppliedIndirection = false;
  for (unsigned I = 0, E = CPUsers.size(); I != E; ++I) {
    CPUser &U = CPUsers[I];
    if (!U.CPEMI || !U.MI)
      continue;
    unsigned UserOff = getUserOffset(U);
    unsigned CPEOff = getOffsetOf(U.CPEMI);
    if (isOffsetInRange(UserOff, CPEOff, U.getMaxDisp(), U.NegOk))
      continue;

    // CPI is out of range.  Apply two-level indirection if possible.
    // This works for any opcode that ultimately uses MOVL_PCREL to load
    // a 32-bit value from the constant pool.  MOV_GOT is excluded because
    // it uses a compound mova+mov.l+add sequence where indirection would
    // break the mova PC-relative relationship.
    MachineInstr *UserMI = U.MI;
    unsigned UserOpc = UserMI->getOpcode();
    if (UserOpc == SH::MOV_GOT || UserOpc == SH::BR_FAR) {
      report_fatal_error("SH constant island pass failed to place CPI within "
                         "displacement range (UserOff=" +
                         Twine(UserOff) + ", CPEOff=" + Twine(CPEOff) +
                         ", MaxDisp=" + Twine(U.getMaxDisp()) +
                         ") in function " + MF->getName());
    }

    MachineInstr *CPEMI = U.CPEMI;
    unsigned FarLabelID = CPEMI->getOperand(0).getImm();
    LLVMContext &Ctx = MF->getFunction().getContext();

    auto *TrampolineCPV = SHConstantPoolCPIAddr::Create(Ctx, FarLabelID);
    unsigned TrampolineCPI = MCP->getConstantPoolIndex(TrampolineCPV, Align(4));
    if (TrampolineCPI >= CPEntries.size())
      CPEntries.resize(TrampolineCPI + 1);

    MachineBasicBlock *TrampolineIsland = MF->CreateMachineBasicBlock();
    MachineBasicBlock *UserMBB = UserMI->getParent();
    MF->insert(std::next(UserMBB->getIterator()), TrampolineIsland);
    updateForInsertedWaterBlock(TrampolineIsland);

    unsigned TrampolineID = createPICLabelUId();
    MachineInstr *TrampolineCPEMI =
        BuildMI(*TrampolineIsland, TrampolineIsland->end(), DebugLoc(),
                TII->get(SH::CONSTPOOL_ENTRY))
            .addImm(TrampolineID)
            .addConstantPoolIndex(TrampolineCPI)
            .addImm(4);
    CPEntries[TrampolineCPI].push_back(
        CPEntry(TrampolineCPEMI, TrampolineID, 1));
    ++NumCPEs;
    TrampolineIsland->setAlignment(Align(4));
    BBInfo[TrampolineIsland->getNumber()].Size += 4;
    adjustBBOffsetsAfter(&*std::prev(TrampolineIsland->getIterator()));

    Register DstReg = UserMI->getOperand(0).getReg();
    for (unsigned J = 0, JE = UserMI->getNumOperands(); J != JE; ++J)
      if (UserMI->getOperand(J).isCPI()) {
        UserMI->getOperand(J).setIndex(TrampolineID);
        break;
      }

    MachineBasicBlock::iterator InsertPt = UserMI->getIterator();
    ++InsertPt;
    while (InsertPt != UserMBB->end() && InsertPt->isBundledWithPred())
      ++InsertPt;
    BuildMI(*UserMBB, InsertPt, UserMI->getDebugLoc(), TII->get(SH::MOVL_IND),
            DstReg)
        .addReg(DstReg);

    U.CPEMI = TrampolineCPEMI;
    U.HighWaterMark = TrampolineIsland;
    U.CloneCount = 0;

    LLVM_DEBUG(dbgs() << "  Post-convergence two-level indirection: CPI#"
                      << TrampolineCPI << " -> far label " << FarLabelID
                      << "\n");
    AppliedIndirection = true;
  }

  // If we applied indirection, run a mini convergence loop to place the
  // new trampoline CPIs and add fallthrough protection.
  if (AppliedIndirection) {
    adjustBBOffsetsAfter(&MF->front());
    for (unsigned Iter = 0; Iter < 10; ++Iter) {
      bool CPChange = false;
      for (unsigned I = 0, E = CPUsers.size(); I != E; ++I)
        CPChange |= handleConstantPoolUser(I);
      NewWaterList.clear();
      bool BRChange = fixupBranches();
      bool FTChange = addFallthroughProtection();
      if (!CPChange && !BRChange && !FTChange)
        break;
      adjustBBOffsetsAfter(&MF->front());
    }
  }

  BBInfo.clear();
  WaterList.clear();
  CPUsers.clear();
  CPEntries.clear();

  return MadeChange;
}

void SHConstantIslands::doInitialPlacement(
    std::vector<MachineInstr *> &CPEMIs) {
  MachineBasicBlock *BB = MF->CreateMachineBasicBlock();
  MF->push_back(BB);

  const Align MaxAlign = MCP->getConstantPoolAlign();
  BB->setAlignment(Align(4));
  MF->ensureAlignment(BB->getAlignment());

  SmallVector<MachineBasicBlock::iterator, 8> InsPoint(Log2(MaxAlign) + 1,
                                                       BB->end());

  const std::vector<MachineConstantPoolEntry> &CPs = MCP->getConstants();
  const DataLayout &TD = MF->getDataLayout();

  for (unsigned I = 0, E = CPs.size(); I != E; ++I) {
    unsigned Size = CPs[I].getSizeInBytes(TD);
    Align Alignment = CPs[I].getAlign();

    unsigned LogAlign = Log2(Alignment);
    MachineBasicBlock::iterator InsAt = InsPoint[LogAlign];

    MachineInstr *CPEMI =
        BuildMI(*BB, InsAt, DebugLoc(), TII->get(SH::CONSTPOOL_ENTRY))
            .addImm(I)
            .addConstantPoolIndex(I)
            .addImm(Size);

    CPEMIs.push_back(CPEMI);

    for (unsigned A = LogAlign + 1; A <= Log2(MaxAlign); ++A)
      if (InsPoint[A] == InsAt)
        InsPoint[A] = CPEMI;

    CPEntries.emplace_back(1, CPEntry(CPEMI, I));
    ++NumCPEs;
  }

  // Prevent fallthrough from the preceding block into the constant pool
  // data block.  If the block before the CP block has no terminator (e.g.
  // an empty block generated for the normal continuation of a throwing
  // call), execution would fall through into .long data and crash.
  // Insert a BRA to the CP block's fallthrough successor, or if none
  // exists, add a NOP as a safety measure (the block should be dead code).
  if (BB != &MF->front()) {
    MachineBasicBlock *PrevBB = &*std::prev(BB->getIterator());
    if (!PrevBB->empty() && !PrevBB->back().isTerminator()) {
      // Previous block doesn't end with a terminator — it falls through.
      // Find a non-CP successor to branch to.
      MachineBasicBlock *Target = nullptr;
      for (auto *Succ : PrevBB->successors()) {
        if (Succ != BB) {
          Target = Succ;
          break;
        }
      }
      if (Target) {
        BuildMI(PrevBB, DebugLoc(), TII->get(SH::BRA)).addMBB(Target);
      }
    } else if (PrevBB->empty()) {
      // Empty block with no instructions — falls through to CP block.
      // Find a successor to branch to.
      MachineBasicBlock *Target = nullptr;
      for (auto *Succ : PrevBB->successors()) {
        if (Succ != BB) {
          Target = Succ;
          break;
        }
      }
      if (Target) {
        BuildMI(PrevBB, DebugLoc(), TII->get(SH::BRA)).addMBB(Target);
      }
    }
  }
}

/// Convert MOV_I32/JTI instructions into MOV_I32/CPI by wrapping the JTI
/// address in a SHConstantPoolJTI constant pool entry.  The CI pass then
/// manages the placement of these CPI entries (ensuring they stay within
/// PC-relative reach of the MOV_I32).  The actual jump table data remains
/// in .rodata, emitted by the default AsmPrinter::emitJumpTableInfo.
void SHConstantIslands::doInitialJumpTablePlacement(
    std::vector<MachineInstr *> &CPEMIs) {
  auto *MJTI = MF->getJumpTableInfo();
  if (!MJTI)
    return;

  // Collect all MOV_I32 instructions with JTI operands.
  SmallVector<std::pair<MachineInstr *, unsigned>, 4> JTUsers;
  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != SH::MOV_I32)
        continue;
      const MachineOperand &MO = MI.getOperand(1);
      if (MO.isJTI())
        JTUsers.push_back({&MI, MO.getIndex()});
    }
  }

  if (JTUsers.empty())
    return;

  // Each JTI address is a 4-byte pointer (same as any other constant).
  const unsigned EntrySize = 4;

  // Get or create the BB at the end of the function for CP data.
  // We reuse the existing CP block if one was created by doInitialPlacement.
  MachineBasicBlock *BB = &MF->back();
  // If the last block has real code (not just CONSTPOOL_ENTRYs), create a new
  // one.
  bool NeedNewBB = BB->empty();
  if (!NeedNewBB) {
    // Check if the last BB is already a CP data block.
    NeedNewBB = (BB->front().getOpcode() != SH::CONSTPOOL_ENTRY);
  }
  if (NeedNewBB) {
    BB = MF->CreateMachineBasicBlock();
    MF->push_back(BB);
    BB->setAlignment(Align(4));
  }

  MachineConstantPool *CP = MF->getConstantPool();

  // Track which JTI indices we've already created CPIs for, to avoid
  // duplicate CONSTPOOL_ENTRYs when multiple MOV_I32s reference the
  // same jump table.
  DenseMap<unsigned, unsigned> JTItoCPI;

  for (auto &[MI, JTI] : JTUsers) {
    unsigned CPI;
    auto It = JTItoCPI.find(JTI);
    if (It == JTItoCPI.end()) {
      // First time seeing this JTI — create a new CPI and CONSTPOOL_ENTRY.
      auto *CPV =
          SHConstantPoolJTI::Create(MF->getFunction().getContext(), JTI);
      CPI = CP->getConstantPoolIndex(CPV, Align(4));
      JTItoCPI[JTI] = CPI;

      MachineInstr *CPEMI =
          BuildMI(*BB, BB->end(), DebugLoc(), TII->get(SH::CONSTPOOL_ENTRY))
              .addImm(CPI)
              .addConstantPoolIndex(CPI)
              .addImm(EntrySize);

      CPEMIs.push_back(CPEMI);
      CPEntries.emplace_back(1, CPEntry(CPEMI, CPI));
      ++NumCPEs;
    } else {
      // Already created — reuse the CPI.
      CPI = It->second;
    }

    // Rewrite MOV_I32/JTI → MOV_I32/CPI.
    MachineOperand &MO = MI->getOperand(1);
    MO.ChangeToImmediate(0); // clear to a safe type first
    MI->removeOperand(1);    // remove the old operand
    MachineInstrBuilder(*MF, MI).addConstantPoolIndex(CPI);
  }
}

static bool bbHasFallthrough(MachineBasicBlock *MBB) {
  MachineFunction::iterator MBBI = MBB->getIterator();
  if (std::next(MBBI) == MBB->getParent()->end())
    return false;

  MachineBasicBlock *NextBB = &*std::next(MBBI);
  for (MachineBasicBlock::succ_iterator I = MBB->succ_begin(),
                                        E = MBB->succ_end();
       I != E; ++I)
    if (*I == NextBB)
      return true;

  return false;
}

SHConstantIslands::CPEntry *
SHConstantIslands::findConstPoolEntry(unsigned CPI, const MachineInstr *CPEMI) {
  std::vector<CPEntry> &CPEs = CPEntries[CPI];
  for (unsigned I = 0, E = CPEs.size(); I != E; ++I) {
    if (CPEs[I].CPEMI == CPEMI)
      return &CPEs[I];
  }
  return nullptr;
}

Align SHConstantIslands::getCPEAlign(const MachineInstr &CPEMI) {
  assert(CPEMI.getOpcode() == SH::CONSTPOOL_ENTRY);
  unsigned CPI = CPEMI.getOperand(1).getIndex();
  return MCP->getConstants()[CPI].getAlign();
}

void SHConstantIslands::initializeFunctionInfo(
    const std::vector<MachineInstr *> &CPEMIs) {
  BBInfo.clear();
  BBInfo.resize(MF->getNumBlockIDs());

  // Compute block sizes and offsets iteratively.
  // Pass 1: compute sizes with offset=0 (approximate).
  for (MachineFunction::iterator I = MF->begin(), E = MF->end(); I != E; ++I)
    computeBlockSize(&*I);
  BBInfo[MF->front().getNumber()].Offset = 0;
  // The known bits of the entry block offset are determined by the function
  // alignment.
  BBInfo[MF->front().getNumber()].KnownBits = Log2(MF->getAlignment());
  adjustBBOffsetsAfter(&MF->front());

  // Pass 2: recompute sizes with correct offsets (alignment-aware).
  for (MachineFunction::iterator I = MF->begin(), E = MF->end(); I != E; ++I)
    computeBlockSize(&*I);
  adjustBBOffsetsAfter(&MF->front());

  for (MachineBasicBlock &MBB : *MF) {
    if (!bbHasFallthrough(&MBB))
      WaterList.push_back(&MBB);

    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;

      int Opc = MI.getOpcode();

      // Branch fixup is handled by SHBranchExpansion pass.
      // CI pass only tracks CPI users.

      if (Opc == SH::CONSTPOOL_ENTRY)
        continue;

      for (unsigned Op = 0, E = MI.getNumOperands(); Op != E; ++Op) {
        if (MI.getOperand(Op).isCPI()) {
          unsigned MaxOffs = 0;
          bool NegOk = false;

          switch (Opc) {
          case SH::MOVL_PCREL:
          case SH::MOV_I32:
            // Hardware max is 255*4=1020.
            MaxOffs = 255 * 4;
            break;
          case SH::MOVW_PCREL:
            MaxOffs = 255 * 2;
            break;
          default:
            MaxOffs = 255 * 4;
            break;
          }

          unsigned CPI = MI.getOperand(Op).getIndex();
          MachineInstr *CPEMI = CPEMIs[CPI];
          CPUsers.push_back(CPUser(&MI, CPEMI, MaxOffs, NegOk));

          CPEntry *CPE = findConstPoolEntry(CPI, CPEMI);
          CPE->RefCount++;

          // If this CPI wraps another CPI's address (SHConstantPoolCPIAddr),
          // bump the referenced data CPI's RefCount too to prevent removal.
          const MachineConstantPoolEntry &MCPE = MCP->getConstants()[CPI];
          if (MCPE.isMachineConstantPoolEntry()) {
            if (auto *CAddr = dyn_cast<SHConstantPoolCPIAddr>(
                    static_cast<SHConstantPoolValue *>(
                        MCPE.Val.MachineCPVal))) {
              unsigned DataCPI = CAddr->getCPIndex();
              CPEntry *DataCPE = findConstPoolEntry(DataCPI, CPEMIs[DataCPI]);
              if (DataCPE)
                DataCPE->RefCount++;
            }
          }
        }
      }
    }
  }
}

void SHConstantIslands::computeBlockSize(MachineBasicBlock *MBB) {
  BasicBlockInfo &BBI = BBInfo[MBB->getNumber()];
  BBI.Size = 0;
  // Use instrs() (instr_begin/instr_end) instead of the top-level iterator.
  // MIBundleBuilder creates bundles where the first real instruction is
  // the bundle head — NOT a BUNDLE pseudo.  The top-level iterator visits
  // only the head, so getInstSizeInBytes(head) returns its own 2 bytes
  // without the bundled successor (NOP delay slot).  instr_begin visits
  // every instruction including bundle internals, giving the correct total.
  for (const MachineInstr &MI : MBB->instrs()) {
    if (MI.getOpcode() == SH::CONSTPOOL_ENTRY)
      BBI.Size += MI.getOperand(2).getImm();
    else
      BBI.Size += TII->getInstSizeInBytes(MI);
  }
}

unsigned SHConstantIslands::getOffsetOf(MachineInstr *MI) const {
  MachineBasicBlock *MBB = MI->getParent();
  unsigned Offset = BBInfo[MBB->getNumber()].Offset;
  // CPI alignment is handled at BB level, not per-entry.
  // Use instr_begin to visit bundle internals — see computeBlockSize comment.
  for (MachineBasicBlock::instr_iterator I = MBB->instr_begin(); &*I != MI;
       ++I) {
    if (I->getOpcode() == SH::CONSTPOOL_ENTRY)
      Offset += I->getOperand(2).getImm();
    else
      Offset += TII->getInstSizeInBytes(*I);
  }
  return Offset;
}

static bool compareMbbNumbers(const MachineBasicBlock *LHS,
                              const MachineBasicBlock *RHS) {
  return LHS->getNumber() < RHS->getNumber();
}

void SHConstantIslands::updateForInsertedWaterBlock(MachineBasicBlock *NewBB) {
  NewBB->getParent()->RenumberBlocks();
  BBInfo.insert(BBInfo.begin() + NewBB->getNumber(), BasicBlockInfo());
  water_iterator IP = llvm::lower_bound(WaterList, NewBB, compareMbbNumbers);
  WaterList.insert(IP, NewBB);
}

unsigned SHConstantIslands::getUserOffset(CPUser &U) const {
  unsigned UserOffset = getOffsetOf(U.MI);
  // SH MOV.L @(disp,PC) uses base = (PC & ~3) where PC = InstrAddr + 4.
  return (UserOffset + 4) & ~3u;
}

MachineBasicBlock *SHConstantIslands::splitBlockBeforeInstr(MachineInstr &MI) {
  MachineBasicBlock *OrigBB = MI.getParent();
  MachineBasicBlock *NewBB =
      MF->CreateMachineBasicBlock(OrigBB->getBasicBlock());
  MachineFunction::iterator MBBI = ++OrigBB->getIterator();
  MF->insert(MBBI, NewBB);

  NewBB->splice(NewBB->end(), OrigBB, MI, OrigBB->end());

  BuildMI(OrigBB, DebugLoc(), TII->get(SH::BRA)).addMBB(NewBB);
  BuildMI(OrigBB, DebugLoc(), TII->get(SH::NOP))
      .getInstr()
      ->bundleWithPred(); // bundle NOP with BRA as delay slot
  ++NumSplit;

  NewBB->transferSuccessors(OrigBB);
  OrigBB->addSuccessor(NewBB);

  MF->RenumberBlocks();
  BBInfo.insert(BBInfo.begin() + NewBB->getNumber(), BasicBlockInfo());

  water_iterator IP = llvm::lower_bound(WaterList, OrigBB, compareMbbNumbers);
  if (IP != WaterList.end() && *IP == OrigBB)
    WaterList.insert(std::next(IP), NewBB);
  else
    WaterList.insert(IP, OrigBB);
  NewWaterList.insert(OrigBB);

  computeBlockSize(OrigBB);
  computeBlockSize(NewBB);
  adjustBBOffsetsAfter(OrigBB);

  return NewBB;
}

bool SHConstantIslands::isOffsetInRange(unsigned UserOffset,
                                        unsigned TrialOffset, unsigned MaxDisp,
                                        bool NegativeOK) {
  if (UserOffset <= TrialOffset) {
    if (TrialOffset - UserOffset <= MaxDisp)
      return true;
  } else if (NegativeOK) {
    if (UserOffset - TrialOffset <= MaxDisp)
      return true;
  }
  return false;
}

bool SHConstantIslands::isOffsetInRange(unsigned UserOffset,
                                        unsigned TrialOffset, const CPUser &U) {
  return isOffsetInRange(UserOffset, TrialOffset, U.getMaxDisp(), U.NegOk);
}

bool SHConstantIslands::isWaterInRange(unsigned UserOffset,
                                       MachineBasicBlock *Water, CPUser &U,
                                       unsigned &Growth) {
  MachineFunction::const_iterator NextBlock = ++Water->getIterator();
  Align NextBlockAlignment =
      (NextBlock == MF->end()) ? Align(4) : NextBlock->getAlignment();
  // Use postOffset with the successor alignment to include worst-case
  // alignment padding in the offset calculation.
  unsigned CPEOffset =
      BBInfo[Water->getNumber()].postOffset(NextBlockAlignment);
  unsigned NextBlockOffset;
  if (NextBlock == MF->end()) {
    NextBlockOffset = CPEOffset;
  } else {
    NextBlockOffset = BBInfo[NextBlock->getNumber()].Offset;
  }
  unsigned Size = U.CPEMI->getOperand(2).getImm();
  unsigned CPEEnd = CPEOffset + Size;

  if (CPEEnd > NextBlockOffset) {
    Growth = CPEEnd - NextBlockOffset;
    Growth += offsetToAlignment(CPEEnd, NextBlockAlignment);
    if (CPEOffset < UserOffset)
      UserOffset += Growth;
  } else
    Growth = 0;

  return isOffsetInRange(UserOffset, CPEOffset, U);
}

bool SHConstantIslands::isCPEntryInRange(MachineInstr *MI, unsigned UserOffset,
                                         MachineInstr *CPEMI, unsigned MaxDisp,
                                         bool NegOk, bool DoDump) {
  unsigned CPEOffset = getOffsetOf(CPEMI);
  return isOffsetInRange(UserOffset, CPEOffset, MaxDisp, NegOk);
}

void SHConstantIslands::adjustBBOffsetsAfter(MachineBasicBlock *BB) {
  unsigned BBNum = BB->getNumber();
  for (unsigned I = BBNum + 1, E = MF->getNumBlockIDs(); I < E; ++I) {
    if (I >= BBInfo.size())
      break;
    // Get the offset and known bits at the end of the layout predecessor.
    // Include worst-case alignment padding for the current block.
    const Align A = MF->getBlockNumbered(I)->getAlignment();
    const unsigned Offset = BBInfo[I - 1].postOffset(A);
    const unsigned KB = llvm::countr_zero(Offset);

    // Always propagate through all blocks to guarantee offsets are never
    // stale.  The early-stop optimization was removed because coincidental
    // offset matches could mask real changes further down the chain.

    BBInfo[I].Offset = Offset;
    BBInfo[I].KnownBits = KB;
  }
}

bool SHConstantIslands::decrementCPEReferenceCount(unsigned CPI,
                                                   MachineInstr *CPEMI) {
  CPEntry *CPE = findConstPoolEntry(CPI, CPEMI);
  assert(CPE && "Unexpected!");
  if (--CPE->RefCount == 0) {
    removeDeadCPEMI(CPEMI);
    CPE->CPEMI = nullptr;
    --NumCPEs;
    return true;
  }
  return false;
}

int SHConstantIslands::findInRangeCPEntry(CPUser &U, unsigned UserOffset) {
  MachineInstr *UserMI = U.MI;
  MachineInstr *CPEMI = U.CPEMI;

  if (isCPEntryInRange(UserMI, UserOffset, CPEMI, U.getMaxDisp(), U.NegOk,
                       true))
    return 1;

  unsigned CPI = CPEMI->getOperand(1).getIndex();
  std::vector<CPEntry> &CPEs = CPEntries[CPI];
  for (unsigned I = 0, E = CPEs.size(); I != E; ++I) {
    if (CPEs[I].CPEMI == CPEMI)
      continue;
    if (CPEs[I].CPEMI == nullptr)
      continue;
    if (isCPEntryInRange(UserMI, UserOffset, CPEs[I].CPEMI, U.getMaxDisp(),
                         U.NegOk)) {
      U.CPEMI = CPEs[I].CPEMI;
      for (unsigned J = 0, E = UserMI->getNumOperands(); J != E; ++J)
        if (UserMI->getOperand(J).isCPI()) {
          UserMI->getOperand(J).setIndex(CPEs[I].CPI);
          break;
        }
      CPEs[I].RefCount++;
      return decrementCPEReferenceCount(CPI, CPEMI) ? 2 : 1;
    }
  }
  return 0;
}

bool SHConstantIslands::findAvailableWater(CPUser &U, unsigned UserOffset,
                                           water_iterator &WaterIter) {
  if (WaterList.empty())
    return false;

  unsigned BestGrowth = ~0u;
  for (water_iterator IP = std::prev(WaterList.end()), B = WaterList.begin();;
       --IP) {
    MachineBasicBlock *WaterBB = *IP;
    unsigned Growth;
    if (isWaterInRange(UserOffset, WaterBB, U, Growth) &&
        (WaterBB->getNumber() < U.HighWaterMark->getNumber() ||
         NewWaterList.count(WaterBB)) &&
        Growth < BestGrowth) {
      BestGrowth = Growth;
      WaterIter = IP;
      if (BestGrowth == 0)
        return true;
    }
    if (IP == B)
      break;
  }
  return BestGrowth != ~0u;
}

void SHConstantIslands::createNewWater(unsigned CPUserIndex,
                                       unsigned UserOffset,
                                       MachineBasicBlock *&NewMBB) {
  CPUser &U = CPUsers[CPUserIndex];
  MachineInstr *UserMI = U.MI;
  MachineInstr *CPEMI = U.CPEMI;
  MachineBasicBlock *UserMBB = UserMI->getParent();
  const BasicBlockInfo &UserBBI = BBInfo[UserMBB->getNumber()];

  // Check if we can place the island after the user's block.
  // This works for both fallthrough blocks (add a BRA first) and
  // non-fallthrough blocks (already end with BRA).
  MachineFunction::iterator NextBlock = ++UserMBB->getIterator();
  if (NextBlock != MF->end()) {
    unsigned Delta = bbHasFallthrough(UserMBB) ? 4 : 0; // BRA size if needed
    unsigned CPEOffset = UserBBI.postOffset() + Delta;

    if (isOffsetInRange(UserOffset, CPEOffset, U)) {
      NewMBB = &*NextBlock;
      if (bbHasFallthrough(UserMBB)) {
        // Add unconditional branch to the fall-through block
        int UncondBr = SH::BRA;
        BuildMI(UserMBB, DebugLoc(), TII->get(UncondBr)).addMBB(NewMBB);
        BuildMI(UserMBB, DebugLoc(), TII->get(SH::NOP))
            .getInstr()
            ->bundleWithPred();
        // Size: BRA(2) + NOP(2) = 4 bundled
        BBInfo[UserMBB->getNumber()].Size += 4;
      }
      adjustBBOffsetsAfter(UserMBB);
      return;
    }
  }

  const Align AlignInfo = MF->getAlignment();
  unsigned BaseInsertOffset = UserOffset + U.getMaxDisp();
  BaseInsertOffset -= 4;

  if (BaseInsertOffset + 8 >= UserBBI.postOffset()) {
    BaseInsertOffset = UserBBI.postOffset() - 8;
  }
  unsigned EndInsertOffset =
      BaseInsertOffset + 4 + CPEMI->getOperand(2).getImm();
  MachineBasicBlock::iterator MI = UserMI;
  ++MI;
  unsigned CPUIndex = CPUserIndex + 1;
  unsigned NumCPUsers = CPUsers.size();
  // Helper: compute the full size of MI including any bundled internals.
  // MIBundleBuilder doesn't insert a BUNDLE pseudo, so the top-level
  // iterator only sees the bundle HEAD.  We must also count the
  // bundled NOP delay slot that follows.
  auto fullInstSize = [this](const MachineInstr &I) -> unsigned {
    unsigned Sz = TII->getInstSizeInBytes(I);
    if (I.isBundledWithSucc()) {
      auto It = I.getIterator();
      ++It;
      while (It->isBundledWithPred()) {
        Sz += TII->getInstSizeInBytes(*It);
        ++It;
      }
    }
    return Sz;
  };
  for (unsigned Offset = UserOffset + fullInstSize(*UserMI);
       MI != UserMBB->end() && Offset < BaseInsertOffset;
       Offset += fullInstSize(*MI), MI = std::next(MI)) {
    if (CPUIndex < NumCPUsers && CPUsers[CPUIndex].MI == MI) {
      CPUser &U = CPUsers[CPUIndex];
      if (!isOffsetInRange(Offset, EndInsertOffset, U)) {
        BaseInsertOffset -= AlignInfo.value();
        EndInsertOffset -= AlignInfo.value();
      }
      EndInsertOffset += U.CPEMI->getOperand(2).getImm();
      CPUIndex++;
    }
  }

  NewMBB = splitBlockBeforeInstr(*--MI);
}

bool SHConstantIslands::handleConstantPoolUser(unsigned CPUserIndex) {
  CPUser &U = CPUsers[CPUserIndex];
  MachineInstr *UserMI = U.MI;
  MachineInstr *CPEMI = U.CPEMI;
  if (!CPEMI)
    return false;
  unsigned CPI = CPEMI->getOperand(1).getIndex();
  unsigned Size = CPEMI->getOperand(2).getImm();
  unsigned UserOffset = getUserOffset(U);

  int result = findInRangeCPEntry(U, UserOffset);

  if (result == 1)
    return false;
  if (result == 2)
    return true;

  // Anti-oscillation: after many re-clonings, prefer cheaper strategies
  // that don't create new MBBs (which cause cascading displacement growth).
  // If those all fail, fall through to normal island creation as a last
  // resort — we never silently leave a CPI out of range.
  if (U.CloneCount >= 10) {
    // Try inline materialization first (eliminates the CPI entirely).
    if (materializeConstantInline(U))
      return true;

    // Try packing into an existing island MBB.
    for (MachineFunction::iterator MBI = MF->begin(), MBE = MF->end();
         MBI != MBE; ++MBI) {
      MachineBasicBlock &Candidate = *MBI;
      if (Candidate.empty())
        continue;
      bool AllCPE = true;
      for (const MachineInstr &MI : Candidate) {
        if (MI.getOpcode() != SH::CONSTPOOL_ENTRY) {
          AllCPE = false;
          break;
        }
      }
      if (!AllCPE)
        continue;
      if (Candidate.getNumber() > U.HighWaterMark->getNumber() &&
          !NewWaterList.count(&Candidate))
        continue;
      unsigned IslandEnd = BBInfo[Candidate.getNumber()].postOffset(
          getCPEAlign(Candidate.back()));
      if (!isOffsetInRange(UserOffset, IslandEnd, U))
        continue;

      // Pack into the existing island.
      decrementCPEReferenceCount(CPI, CPEMI);
      unsigned ID = createPICLabelUId();
      U.HighWaterMark = &Candidate;
      U.CPEMI = BuildMI(Candidate, Candidate.end(), DebugLoc(),
                        TII->get(SH::CONSTPOOL_ENTRY))
                    .addImm(ID)
                    .addConstantPoolIndex(CPI)
                    .addImm(Size);
      CPEntries[CPI].push_back(CPEntry(U.CPEMI, ID, 1));
      ++NumCPEs;
      BBInfo[Candidate.getNumber()].Size += Size;
      adjustBBOffsetsAfter(&*--Candidate.getIterator());
      U.CloneCount++;
      for (unsigned I = 0, E = UserMI->getNumOperands(); I != E; ++I)
        if (UserMI->getOperand(I).isCPI()) {
          UserMI->getOperand(I).setIndex(ID);
          break;
        }
      LLVM_DEBUG(dbgs() << "  Packed CPI#" << CPI << " into island BB#"
                        << Candidate.getNumber() << "\n");
      return true;
    }
    // Both materialization and packing failed.  As a last resort, use
    // two-level indirection: create a nearby trampoline CPI that holds
    // the ADDRESS of the far CPI data, then rewrite the user instruction
    // from a single MOV_I32 (MOVL_PCREL) into:
    //   MOV_I32  Rn, @trampoline  ; loads address of far CPI (reachable)
    // This works for any CPI type (globals, symbols, block addresses, etc.)
    // because we never move the far CPI — we just add a reachable pointer
    // to it.  The trampoline is only 4 bytes so it fits easily.
    // MOV_GOT is excluded because it uses a compound mova+mov.l+add
    // sequence where indirection would break the mova relationship.
    if (UserMI->getOpcode() != SH::MOV_GOT) {
      // The current CPEMI's label ID is what SHConstantPoolCPIAddr wraps.
      unsigned FarLabelID = CPEMI->getOperand(0).getImm();
      LLVMContext &Ctx = MF->getFunction().getContext();

      // Create a SHConstantPoolCPIAddr entry that emits
      // ".long .LCPI_<FarLabelID>" — the absolute address of the far CPI.
      auto *TrampolineCPV = SHConstantPoolCPIAddr::Create(Ctx, FarLabelID);
      unsigned TrampolineCPI =
          MCP->getConstantPoolIndex(TrampolineCPV, Align(4));

      // Ensure CPEntries has room for the new CPI index.
      if (TrampolineCPI >= CPEntries.size())
        CPEntries.resize(TrampolineCPI + 1);

      // Build a CONSTPOOL_ENTRY for the trampoline.  We place it in a new
      // island right after the user's block — since the trampoline is only
      // 4 bytes, it will easily fit within the 1020-byte displacement.
      MachineBasicBlock *TrampolineIsland = MF->CreateMachineBasicBlock();
      MachineBasicBlock *UserMBB = UserMI->getParent();
      MF->insert(std::next(UserMBB->getIterator()), TrampolineIsland);
      updateForInsertedWaterBlock(TrampolineIsland);

      unsigned TrampolineID = createPICLabelUId();
      MachineInstr *TrampolineCPEMI =
          BuildMI(*TrampolineIsland, TrampolineIsland->end(), DebugLoc(),
                  TII->get(SH::CONSTPOOL_ENTRY))
              .addImm(TrampolineID)
              .addConstantPoolIndex(TrampolineCPI)
              .addImm(4); // 4-byte entry
      CPEntries[TrampolineCPI].push_back(
          CPEntry(TrampolineCPEMI, TrampolineID, 1));
      ++NumCPEs;
      TrampolineIsland->setAlignment(Align(4));
      BBInfo[TrampolineIsland->getNumber()].Size += 4;
      adjustBBOffsetsAfter(&*std::prev(TrampolineIsland->getIterator()));

      // Rewrite the user: point CPI operand at the trampoline.
      Register DstReg = UserMI->getOperand(0).getReg();
      for (unsigned I = 0, E = UserMI->getNumOperands(); I != E; ++I)
        if (UserMI->getOperand(I).isCPI()) {
          UserMI->getOperand(I).setIndex(TrampolineID);
          break;
        }

      // Insert MOV.L @Rn, Rn right after the user instruction (and after
      // any bundled delay slot).
      MachineBasicBlock::iterator InsertPt = UserMI->getIterator();
      ++InsertPt;
      while (InsertPt != UserMBB->end() && InsertPt->isBundledWithPred())
        ++InsertPt;
      BuildMI(*UserMBB, InsertPt, UserMI->getDebugLoc(), TII->get(SH::MOVL_IND),
              DstReg)
          .addReg(DstReg);

      // Update the CPUser to point to the trampoline CPEMI.
      U.CPEMI = TrampolineCPEMI;
      U.HighWaterMark = TrampolineIsland;
      U.CloneCount = 0; // Reset — this is a fresh CPI user now.

      LLVM_DEBUG(dbgs() << "  Two-level CPI indirection: trampoline CPI#"
                        << TrampolineCPI << " -> far label " << FarLabelID
                        << "\n");
      return true;
    }
    // MOV_GOT can't use two-level indirection — fall through
    // to normal island creation as last resort.
  }
  // --- Fall back to creating a new island MBB ---
  MachineBasicBlock *NewIsland = MF->CreateMachineBasicBlock();
  MachineBasicBlock *NewMBB;
  water_iterator IP;
  bool FoundWater = findAvailableWater(U, UserOffset, IP);
  if (FoundWater) {
    MachineBasicBlock *WaterBB = *IP;
    if (NewWaterList.erase(WaterBB))
      NewWaterList.insert(NewIsland);
    NewMBB = &*++WaterBB->getIterator();
  } else {
    createNewWater(CPUserIndex, UserOffset, NewMBB);
    MachineBasicBlock *WaterBB = &*--NewMBB->getIterator();
    IP = llvm::find(WaterList, WaterBB);
    if (IP != WaterList.end())
      NewWaterList.erase(WaterBB);
    NewWaterList.insert(NewIsland);
  }

  if (IP != WaterList.end())
    WaterList.erase(IP);

  MF->insert(NewMBB->getIterator(), NewIsland);
  updateForInsertedWaterBlock(NewIsland);
  decrementCPEReferenceCount(CPI, CPEMI);

  unsigned ID = createPICLabelUId();
  U.HighWaterMark = NewIsland;
  U.CPEMI = BuildMI(NewIsland, DebugLoc(), TII->get(SH::CONSTPOOL_ENTRY))
                .addImm(ID)
                .addConstantPoolIndex(CPI)
                .addImm(Size);
  CPEntries[CPI].push_back(CPEntry(U.CPEMI, ID, 1));
  ++NumCPEs;

  NewIsland->setAlignment(getCPEAlign(*U.CPEMI));
  BBInfo[NewIsland->getNumber()].Size += Size;
  adjustBBOffsetsAfter(&*--NewIsland->getIterator());

  U.CloneCount++;

  for (unsigned I = 0, E = UserMI->getNumOperands(); I != E; ++I)
    if (UserMI->getOperand(I).isCPI()) {
      UserMI->getOperand(I).setIndex(ID);
      break;
    }

  return true;
}

/// materializeConstantInline - When a MOV_I32 can't reach its constant pool
/// entry, try to replace it with an inline instruction
/// sequence that synthesizes the constant value.  Returns true if successful.
bool SHConstantIslands::materializeConstantInline(CPUser &U) {
  MachineInstr *UserMI = U.MI;
  MachineInstr *CPEMI = U.CPEMI;

  // Only handle MOV_I32 — other CPI users (MOVL_PCREL, etc.) stay as CPI.
  if (UserMI->getOpcode() != SH::MOV_I32)
    return false;

  // Extract the CPI entry to get the constant value.
  unsigned CPI = CPEMI->getOperand(1).getIndex();
  const MachineConstantPoolEntry &MCPE = MCP->getConstants()[CPI];

  // Can't materialize target-specific entries (JTI wrappers, CPI addresses).
  if (MCPE.isMachineConstantPoolEntry())
    return false;

  // Can only materialize ConstantInt — not globals, block addresses, etc.
  const Constant *C = MCPE.Val.ConstVal;
  const auto *CI = dyn_cast<ConstantInt>(C);
  if (!CI)
    return false;

  int64_t Val = CI->getSExtValue();

  // The destination register of the MOV_I32.
  Register DstReg = UserMI->getOperand(0).getReg();
  MachineBasicBlock *MBB = UserMI->getParent();
  DebugLoc DL = UserMI->getDebugLoc();

  // Check if we can synthesize this value inline.
  // Strategy: replace one MOV_I32 (2B code + 4B CPI + alignment overhead)
  // with a short instruction sequence (4-6B code, no CPI).
  bool Materialized = false;

  if (Val >= -128 && Val <= 127) {
    // Already fits in MOV #imm8, Rn — shouldn't be a CPI.  Handle anyway.
    BuildMI(*MBB, UserMI, DL, TII->get(SH::MOV_I8), DstReg).addImm(Val);
    Materialized = true;

  } else if (Val >= 128 && Val <= 255) {
    // MOV #val, Rn (sign-extended) + EXTU.B Rn, Rn
    BuildMI(*MBB, UserMI, DL, TII->get(SH::MOV_I8), DstReg)
        .addImm(static_cast<int8_t>(Val));
    BuildMI(*MBB, UserMI, DL, TII->get(SH::EXTU_B), DstReg).addReg(DstReg);
    Materialized = true;

  } else if (Val >= 256 && Val <= 65535) {
    int Hi = (Val >> 8) & 0xFF;
    int Lo = Val & 0xFF;
    // Sign-extend Hi for MOV_I8 (which sign-extends the 8-bit immediate).
    int8_t HiSigned = static_cast<int8_t>(Hi);

    if (Lo == 0) {
      // High byte only: MOV #hi, Rn + SHLL8 Rn
      BuildMI(*MBB, UserMI, DL, TII->get(SH::MOV_I8), DstReg).addImm(HiSigned);
      BuildMI(*MBB, UserMI, DL, TII->get(SH::SHLL8), DstReg).addReg(DstReg);
      // If Hi was negative (>= 128), SHLL8 preserves garbage upper bits.
      // Need EXTU.W to zero-extend.
      if (Hi >= 128) {
        BuildMI(*MBB, UserMI, DL, TII->get(SH::EXTU_W), DstReg).addReg(DstReg);
      }
      Materialized = true;
    } else if (Lo <= 127) {
      // MOV #hi, Rn + SHLL8 Rn + ADD #lo, Rn
      BuildMI(*MBB, UserMI, DL, TII->get(SH::MOV_I8), DstReg).addImm(HiSigned);
      BuildMI(*MBB, UserMI, DL, TII->get(SH::SHLL8), DstReg).addReg(DstReg);
      if (Hi >= 128) {
        BuildMI(*MBB, UserMI, DL, TII->get(SH::EXTU_W), DstReg).addReg(DstReg);
      }
      BuildMI(*MBB, UserMI, DL, TII->get(SH::ADD_I8), DstReg)
          .addReg(DstReg)
          .addImm(Lo);
      Materialized = true;
    }
    // Lo > 127: skip — would need OR_I8 which is R0-only.

  } else if (Val >= -256 && Val <= -129) {
    // Negative values that don't fit in MOV_I8 but can be done with
    // MOV #(val>>8), Rn + SHLL8 Rn (since val>>8 is -1 ==> 0xFFFFFF00).
    // Actually -256..-129: val>>8 = -1 for -256..-1 (NOT useful).
    // Better: MOV #val, Rn (sign-extends -128..127) then adjust.
    // E.g., -200: MOV #56, Rn + NEG Rn, Rn — but we don't have NEG easily.
    // Skip for now — not worth the complexity.
  }

  if (!Materialized)
    return false;

  // Successfully materialized: remove the MOV_I32 and decrement CPE ref.
  UserMI->eraseFromParent();
  U.MI = nullptr;
  U.CPEMI = nullptr;
  decrementCPEReferenceCount(CPI, CPEMI);

  // Recompute block size after replacement.
  computeBlockSize(MBB);
  adjustBBOffsetsAfter(MBB);

  LLVM_DEBUG(dbgs() << "  Materialized CPI#" << CPI << " = " << Val
                    << " inline\n");

  return true;
}

void SHConstantIslands::removeDeadCPEMI(MachineInstr *CPEMI) {
  MachineBasicBlock *CPEBB = CPEMI->getParent();
  if (!CPEBB)
    return;
  unsigned Size = CPEMI->getOperand(2).getImm();
  CPEMI->eraseFromParent();
  if (CPEBB->getNumber() >= (int)BBInfo.size())
    return;
  BBInfo[CPEBB->getNumber()].Size -= Size;
  if (CPEBB->empty()) {
    BBInfo[CPEBB->getNumber()].Size = 0;
    CPEBB->setAlignment(Align(4));
  } else {
    CPEBB->setAlignment(getCPEAlign(*CPEBB->begin()));
  }

  adjustBBOffsetsAfter(CPEBB);
}

bool SHConstantIslands::removeUnusedCPEntries() {
  unsigned MadeChange = false;
  for (unsigned I = 0, E = CPEntries.size(); I != E; ++I) {
    std::vector<CPEntry> &CPEs = CPEntries[I];
    for (unsigned J = 0, Ee = CPEs.size(); J != Ee; ++J) {
      if (CPEs[J].RefCount == 0 && CPEs[J].CPEMI) {
        removeDeadCPEMI(CPEs[J].CPEMI);
        CPEs[J].CPEMI = nullptr;
        MadeChange = true;
      }
    }
  }
  return MadeChange;
}

/// Compute the signed byte offset from a branch instruction to its target MBB,
/// using the CI pass's BBInfo offset tracking.
int64_t SHConstantIslands::getBranchOffset(const MachineInstr *MI) const {
  MachineBasicBlock *TargetMBB = nullptr;
  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isMBB()) {
      TargetMBB = MO.getMBB();
      break;
    }
  }
  if (!TargetMBB)
    return 0;

  unsigned BrOffset = getOffsetOf(const_cast<MachineInstr *>(MI));
  // SH PC-relative branches use PC+4 as base
  unsigned PCBase = BrOffset + 4;
  unsigned TargetOffset = BBInfo[TargetMBB->getNumber()].Offset;
  return static_cast<int64_t>(TargetOffset) - static_cast<int64_t>(PCBase);
}

/// After constant pool island placement, re-check all BRA/BT/BF displacements.
/// Islands inserted by the CI pass may have pushed branch targets out of range.
/// Returns true if any branches were expanded.
bool SHConstantIslands::fixupBranches() {
  // Expand out-of-range branches.  We expand one at a time and rescan
  // because expanding a branch (especially BT/BF → inverted+BRA)
  // invalidates iterators and changes block layout.
  bool EverChanged = false;
  bool ExpandedOne;
  do {
    ExpandedOne = false;
    for (MachineBasicBlock &MBB : *MF) {
      for (MachineInstr &MI : MBB) {
        unsigned Opc = MI.getOpcode();
        int64_t MaxDisp = getMaxBranchDisp(Opc);
        if (MaxDisp == 0)
          continue;

        int64_t Offset = getBranchOffset(&MI);
        if (Offset <= MaxDisp && Offset >= -MaxDisp - 2)
          continue;

        // Found an out-of-range branch — expand it.
        DebugLoc DL = MI.getDebugLoc();

        MachineBasicBlock *TargetBB = nullptr;
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isMBB()) {
            TargetBB = MO.getMBB();
            break;
          }
        }
        assert(TargetBB && "Branch without MBB target");

        if (Opc == SH::BRA) {
          // BRA out of range → BR_FAR pseudo with CPI.
          if (MI.isBundledWithSucc()) {
            auto II = MI.getIterator();
            ++II;
            if (II != MBB.instr_end() && II->isBundledWithPred()) {
              II->unbundleFromPred();
              II->eraseFromParent();
            }
          }

          MI.eraseFromParent();

          auto *CPV = SHConstantPoolMBB::Create(MF->getFunction().getContext(),
                                                TargetBB);
          unsigned CPI = MCP->getConstantPoolIndex(CPV, Align(4));
          unsigned LabelId = createPICLabelUId();

          MachineInstr *BrFar =
              BuildMI(MBB, MBB.end(), DL, TII->get(SH::BR_FAR))
                  .addMBB(TargetBB)
                  .addConstantPoolIndex(LabelId);

          MachineBasicBlock *CPEBB = &MF->back();
          MachineInstr *CPEMI = BuildMI(*CPEBB, CPEBB->end(), DebugLoc(),
                                        TII->get(SH::CONSTPOOL_ENTRY))
                                    .addImm(LabelId)
                                    .addConstantPoolIndex(CPI)
                                    .addImm(4);

          if (CPEntries.size() <= CPI)
            CPEntries.resize(CPI + 1);
          CPEntries[CPI].push_back(CPEntry(CPEMI, LabelId, 1));
          CPUsers.push_back(CPUser(BrFar, CPEMI, 1020, false));
          handleConstantPoolUser(CPUsers.size() - 1);

          computeBlockSize(&MBB);
          computeBlockSize(CPEBB);
          adjustBBOffsetsAfter(&MBB);

          LLVM_DEBUG(dbgs()
                     << "  CI: BRA→BR_FAR in BB#" << MBB.getNumber() << "\n");

        } else {
          // BT/BF/BT_S/BF_S out of range → COND_BR_FAR_T/F pseudo.
          unsigned CondFarOpc = (Opc == SH::BT || Opc == SH::BT_S)
                                    ? SH::COND_BR_FAR_T
                                    : SH::COND_BR_FAR_F;

          // Save the insertion point BEFORE erasing MI.  The conditional
          // branch must stay ahead of any following unconditional BRA in
          // the same basic block; inserting at MBB.end() would place it
          // after the BRA, making the trampoline unreachable.
          // Use the next iterator since erasing MI invalidates its own
          // iterator.
          MachineBasicBlock::iterator InsertPt = std::next(MI.getIterator());
          MI.eraseFromParent();

          auto *CPV = SHConstantPoolMBB::Create(MF->getFunction().getContext(),
                                                TargetBB);
          unsigned CPI = MCP->getConstantPoolIndex(CPV, Align(4));
          unsigned LabelId = createPICLabelUId();

          MachineInstr *CondBrFar =
              BuildMI(MBB, InsertPt, DL, TII->get(CondFarOpc))
                  .addMBB(TargetBB)
                  .addConstantPoolIndex(LabelId);

          MachineBasicBlock *CPEBB = &MF->back();
          MachineInstr *CPEMI = BuildMI(*CPEBB, CPEBB->end(), DebugLoc(),
                                        TII->get(SH::CONSTPOOL_ENTRY))
                                    .addImm(LabelId)
                                    .addConstantPoolIndex(CPI)
                                    .addImm(4);

          if (CPEntries.size() <= CPI)
            CPEntries.resize(CPI + 1);
          CPEntries[CPI].push_back(CPEntry(CPEMI, LabelId, 1));
          CPUsers.push_back(CPUser(CondBrFar, CPEMI, 1020, false));
          handleConstantPoolUser(CPUsers.size() - 1);

          computeBlockSize(&MBB);
          computeBlockSize(CPEBB);
          adjustBBOffsetsAfter(&MBB);

          LLVM_DEBUG(dbgs() << "  CI: far BT/BF→inverted+BRA in BB#"
                            << MBB.getNumber() << "\n");
        }

        ExpandedOne = true;
        EverChanged = true;
        break; // Restart scan — iterators invalidated
      }
      if (ExpandedOne)
        break; // Restart outer loop
    }
  } while (ExpandedOne);

  return EverChanged;
}

/// After all CPI island placement, ensure no code MBB physically falls through
/// into a CONSTPOOL_ENTRY MBB.  The CI pass inserts CPI MBBs between code
/// blocks, but does not always add BRA instructions to skip over them.  If a
/// code MBB has no unconditional terminator and the next layout block is a CPI
/// MBB, execution would run into constant pool data → SIGILL.
bool SHConstantIslands::addFallthroughProtection() {
  bool Changed = false;

  for (MachineFunction::iterator MBBI = MF->begin(), E = MF->end(); MBBI != E;
       ++MBBI) {
    MachineBasicBlock &MBB = *MBBI;

    // Skip CPI MBBs themselves (they only contain CONSTPOOL_ENTRY).
    if (!MBB.empty() && MBB.front().getOpcode() == SH::CONSTPOOL_ENTRY)
      continue;

    // Check if the next block is a CPI MBB.
    auto NextIt = std::next(MBBI);
    if (NextIt == E)
      continue;
    MachineBasicBlock &NextMBB = *NextIt;
    if (NextMBB.empty() || NextMBB.front().getOpcode() != SH::CONSTPOOL_ENTRY)
      continue;

    // The next block is a CPI MBB.  Check if this code block can physically
    // fall through (i.e., it doesn't end with an unconditional branch or
    // return).
    bool HasUncondTerminator = false;
    for (auto II = MBB.rbegin(), IE = MBB.rend(); II != IE; ++II) {
      if (II->isDebugInstr() || II->isInsideBundle())
        continue;
      unsigned Opc = II->getOpcode();
      if (Opc == SH::BRA || Opc == SH::BR_FAR || Opc == SH::JMP ||
          Opc == SH::RTS || II->isReturn()) {
        HasUncondTerminator = true;
      }
      // Note: COND_BR_FAR_T/F are NOT unconditional — execution falls
      // through when the condition is not met.  They need fallthrough
      // protection just like BT/BF.
      break; // Only check the last real instruction
    }

    if (HasUncondTerminator)
      continue;

    // Determine the correct fall-through target. When the block ends
    // with a conditional branch (BT/BF), the fall-through is the path
    // taken when the condition is NOT met. We must use the MBB's
    // successor list (not just the physically next code block), because
    // the CI pass may have inserted CPI islands between MBB and its
    // original fall-through successor.
    MachineBasicBlock *TargetMBB = nullptr;

    // Check if MBB ends with a conditional branch:
    MachineInstr *LastMI = nullptr;
    for (auto II = MBB.rbegin(), IE = MBB.rend(); II != IE; ++II) {
      if (II->isDebugInstr() || II->isInsideBundle())
        continue;
      LastMI = &*II;
      break;
    }

    bool EndsWithCondBranch =
        LastMI &&
        (LastMI->getOpcode() == SH::BT || LastMI->getOpcode() == SH::BF ||
         LastMI->getOpcode() == SH::BT_S || LastMI->getOpcode() == SH::BF_S);

    if (EndsWithCondBranch) {
      // Find the fall-through successor (the one that's NOT the branch target).
      MachineBasicBlock *BranchTarget = nullptr;
      for (const MachineOperand &MO : LastMI->operands()) {
        if (MO.isMBB()) {
          BranchTarget = MO.getMBB();
          break;
        }
      }
      for (MachineBasicBlock *Succ : MBB.successors()) {
        if (Succ != BranchTarget) {
          TargetMBB = Succ;
          break;
        }
      }
    }

    // Fallback: scan for the first code block after the CPI islands.
    if (!TargetMBB) {
      for (auto ScanIt = NextIt; ScanIt != E; ++ScanIt) {
        if (ScanIt->empty() ||
            ScanIt->front().getOpcode() != SH::CONSTPOOL_ENTRY) {
          TargetMBB = &*ScanIt;
          break;
        }
      }
    }
    if (!TargetMBB)
      continue;

    if (!MBB.isSuccessor(TargetMBB))
      MBB.addSuccessor(TargetMBB);

    // Check if the target is within BRA range (±4094 bytes).
    // If not, use BR_FAR with a constant pool entry.
    unsigned MBBEnd = BBInfo[MBB.getNumber()].postOffset();
    unsigned TargetOffset = BBInfo[TargetMBB->getNumber()].Offset;
    int64_t Disp = static_cast<int64_t>(TargetOffset) -
                   static_cast<int64_t>(MBBEnd + 4); // PC+4 base
    if (Disp > 4094 || Disp < -4096) {
      // Out of BRA range — use BR_FAR.
      auto *CPV =
          SHConstantPoolMBB::Create(MF->getFunction().getContext(), TargetMBB);
      unsigned CPI = MCP->getConstantPoolIndex(CPV, Align(4));
      unsigned LabelId = createPICLabelUId();

      MachineInstr *BrFar = BuildMI(&MBB, DebugLoc(), TII->get(SH::BR_FAR))
                                .addMBB(TargetMBB)
                                .addConstantPoolIndex(LabelId);

      MachineBasicBlock *CPEBB = &MF->back();
      MachineInstr *CPEMI = BuildMI(*CPEBB, CPEBB->end(), DebugLoc(),
                                    TII->get(SH::CONSTPOOL_ENTRY))
                                .addImm(LabelId)
                                .addConstantPoolIndex(CPI)
                                .addImm(4);

      if (CPEntries.size() <= CPI)
        CPEntries.resize(CPI + 1);
      CPEntries[CPI].push_back(CPEntry(CPEMI, LabelId, 1));
      CPUsers.push_back(CPUser(BrFar, CPEMI, 1020, false));
      handleConstantPoolUser(CPUsers.size() - 1);
    } else {
      BuildMI(&MBB, DebugLoc(), TII->get(SH::BRA)).addMBB(TargetMBB);
      BuildMI(&MBB, DebugLoc(), TII->get(SH::NOP)).getInstr()->bundleWithPred();
    }

    // Update BBInfo so subsequent getOffsetOf() calls reflect the
    // extra bytes.  Without this, CPE block offsets downstream are
    // stale and the CI/MC displacement diverges.
    computeBlockSize(&MBB);
    adjustBBOffsetsAfter(&MBB);

    LLVM_DEBUG(dbgs() << "  CI: added fallthrough BRA in BB#" << MBB.getNumber()
                      << " → BB#" << TargetMBB->getNumber() << "\n");
    Changed = true;
  }

  return Changed;
}

FunctionPass *llvm::createSHConstantIslandPass() {
  return new SHConstantIslands();
}

INITIALIZE_PASS(SHConstantIslands, DEBUG_TYPE,
                "SH constant island placement and branch shortening pass",
                false, false)
