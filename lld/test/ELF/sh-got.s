# REQUIRES: sh
# RUN: llvm-mc -filetype=obj -triple=sh-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld -shared %t.o -o %t.so
# RUN: llvm-readelf -r %t.so | FileCheck %s

# Test that a shared library with a GOT reference produces dynamic
# relocations in .rela.dyn.

  .text
  .globl foo
  .type foo, @function
foo:
  nop
  rts
  nop

  .data
  .long var@GOT

  .globl var
var:
  .long 42

# Verify a dynamic relocation is produced for the GOT entry.
# CHECK: .rela.dyn
# CHECK: var
