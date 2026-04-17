# REQUIRES: sh
# RUN: llvm-mc -filetype=obj -triple=sh-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o --defsym=target=0x1000 -o %t
# RUN: llvm-readelf -r %t.o | FileCheck --check-prefix=RELOC %s
# RUN: llvm-readelf -x .data %t | FileCheck --check-prefix=DATA %s

# Test R_SH_DIR32 relocation: absolute address resolved at link time.

  .text
  .globl _start
  .type _start, @function
_start:
  nop
  rts
  nop

  .data
  .globl ptr
ptr:
  .long target

# Verify the object file has a relocation in .rela.data.
# RELOC: .rela.data

# Verify the linked data section contains the resolved address
# (0x1000 in little-endian = 00 10 00 00).
# DATA: 00100000
