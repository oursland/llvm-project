; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; CHECK-LABEL: mul_reg:
; CHECK: mul.l	r5, r4
; CHECK: sts	macl, r0
define i32 @mul_reg(i32 %a, i32 %b) {
  %r = mul i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: mul_i64:
; CHECK: dmulu.l
define i64 @mul_i64(i64 %a, i64 %b) {
  %r = mul i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: div_i32:
define i32 @div_i32(i32 %a, i32 %b) {
  %r = sdiv i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: udiv_i32:
define i32 @udiv_i32(i32 %a, i32 %b) {
  %r = udiv i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: srem_i32:
define i32 @srem_i32(i32 %a, i32 %b) {
  %r = srem i32 %a, %b
  ret i32 %r
}
