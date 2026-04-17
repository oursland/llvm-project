# REQUIRES: sh
# RUN: llvm-mc -filetype=obj -triple=sh-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o -o %t 2>&1 | FileCheck --check-prefix=WARN %s --allow-empty
# RUN: llvm-readelf -S %t | FileCheck %s

# Test TLS LE (Local Exec) model: static TLS variable access via TPOFF.

  .text
  .globl _start
  .type _start, @function
_start:
  # Load TLS offset from constant pool, add to GBR (thread pointer).
  mov.l @(4, pc), r0
  stc gbr, r1
  add r1, r0
  rts
  nop
  .p2align 2
  .long tls_var@TPOFF

  .section .tbss,"awT",@nobits
  .globl tls_var
  .type tls_var, @object
  .align 2
tls_var:
  .space 4
  .size tls_var, 4

# Verify the binary links successfully (no errors/warnings).
# WARN-NOT: error
# CHECK: .tbss
