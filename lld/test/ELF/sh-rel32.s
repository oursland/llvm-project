# REQUIRES: sh
# RUN: llvm-mc -filetype=obj -triple=sh-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o -o %t
# RUN: llvm-objdump -d %t | FileCheck %s

# Test R_SH_REL32 relocation: PC-relative data reference.

  .text
  .globl _start
  .type _start, @function
_start:
  mova @(.LCPI0_0, pc), r0
  rts
  nop

.LCPI0_0:
  .long target - .

  .data
  .globl target
target:
  .long 0xdeadbeef

# Verify the code links and disassembles correctly.
# CHECK: <_start>:
# CHECK: mova
