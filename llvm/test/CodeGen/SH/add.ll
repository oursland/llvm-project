; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; CHECK-LABEL: add_reg:
; CHECK: add	r5, r0
define i32 @add_reg(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: add_imm8:
; CHECK: add	##42, r0
define i32 @add_imm8(i32 %a) {
  %r = add i32 %a, 42
  ret i32 %r
}

; CHECK-LABEL: add_neg_imm8:
; CHECK: add	##-10, r0
define i32 @add_neg_imm8(i32 %a) {
  %r = add i32 %a, -10
  ret i32 %r
}

; CHECK-LABEL: add_i64:
; i64 add uses a carry chain
; CHECK: add
; CHECK: add
define i64 @add_i64(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}
