# REQUIRES: sh
# RUN: llvm-mc -filetype=obj -triple=sh-unknown-linux-gnu %s -o %t.o
# RUN: llvm-mc -filetype=obj -triple=sh-unknown-linux-gnu %S/Inputs/sh-plt.s -o %t2.o
# RUN: ld.lld -shared %t2.o -o %t.so
# RUN: ld.lld %t.o %t.so -o %t
# RUN: llvm-readelf -S -r %t | FileCheck %s

# Test dynamic linking with an external shared library function.
# Verify that the linked binary has a .rela.dyn section with a
# dynamic relocation for the external symbol.

  .text
  .globl _start
  .type _start, @function
_start:
  nop
  rts
  nop

  .data
  .long ext_func

# Verify sections and dynamic relocations exist.
# CHECK: .rela.dyn
# CHECK: ext_func
