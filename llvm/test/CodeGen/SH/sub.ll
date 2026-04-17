; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; CHECK-LABEL: sub_reg:
; CHECK: sub	r5, r0
define i32 @sub_reg(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: sub_i64:
; i64 sub uses borrow chain
; CHECK: sub
; CHECK: sub
define i64 @sub_i64(i64 %a, i64 %b) {
  %r = sub i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: neg_reg:
; CHECK: neg	r4, r0
define i32 @neg_reg(i32 %a) {
  %r = sub i32 0, %a
  ret i32 %r
}
