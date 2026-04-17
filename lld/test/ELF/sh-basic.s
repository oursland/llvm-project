# REQUIRES: sh
# RUN: llvm-mc -filetype=obj -triple=sh-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o -o %t
# RUN: llvm-readelf -h %t | FileCheck %s

# Verify SH ELF header is produced correctly.

  .text
  .globl _start
  .type _start, @function
_start:
  mov   #42, r0
  rts
  nop

# CHECK: Class: ELF32
# CHECK: Machine: {{.*}}SH
