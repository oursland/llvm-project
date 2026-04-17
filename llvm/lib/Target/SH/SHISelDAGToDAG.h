//===- SHISelDAGToDAG.h - SH DAG Instruction Selector --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SH_SHISELDAGTODAG_H
#define LLVM_LIB_TARGET_SH_SHISELDAGTODAG_H

#include "SHTargetMachine.h"
#include "llvm/CodeGen/CodeGenCommonISel.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

namespace llvm {

FunctionPass *createSHISelDag(SHTargetMachine &TM, CodeGenOptLevel OptLevel);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SH_SHISELDAGTODAG_H
