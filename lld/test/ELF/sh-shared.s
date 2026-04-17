# REQUIRES: sh
# RUN: llvm-mc -filetype=obj -triple=sh-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld -shared %t.o -o %t.so
# RUN: llvm-readelf -h %t.so | FileCheck %s

# Verify SH shared library generation.

  .text
  .globl foo
  .type foo, @function
foo:
  mov   #1, r0
  rts
  nop

# CHECK: Class: ELF32
# CHECK: Type: DYN
# CHECK: Machine: {{.*}}SH
