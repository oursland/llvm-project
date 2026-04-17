; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test i64 (64-bit integer) operations.

; CHECK-LABEL: add_i64:
define i64 @add_i64(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: sub_i64:
define i64 @sub_i64(i64 %a, i64 %b) {
  %r = sub i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: and_i64:
; CHECK: and
define i64 @and_i64(i64 %a, i64 %b) {
  %r = and i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: or_i64:
; CHECK: or
define i64 @or_i64(i64 %a, i64 %b) {
  %r = or i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: xor_i64:
; CHECK: xor
define i64 @xor_i64(i64 %a, i64 %b) {
  %r = xor i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: shl_i64:
define i64 @shl_i64(i64 %a, i64 %b) {
  %r = shl i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: lshr_i64:
define i64 @lshr_i64(i64 %a, i64 %b) {
  %r = lshr i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: ashr_i64:
define i64 @ashr_i64(i64 %a, i64 %b) {
  %r = ashr i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: load_i64:
; CHECK: mov.l
define i64 @load_i64(ptr %p) {
  %v = load i64, ptr %p
  ret i64 %v
}

; CHECK-LABEL: store_i64:
; CHECK: mov.l
define void @store_i64(ptr %p, i64 %v) {
  store i64 %v, ptr %p
  ret void
}

; CHECK-LABEL: zext_i32_to_i64:
; CHECK: mov	##0, r1
define i64 @zext_i32_to_i64(i32 %a) {
  %r = zext i32 %a to i64
  ret i64 %r
}

; CHECK-LABEL: sext_i32_to_i64:
define i64 @sext_i32_to_i64(i32 %a) {
  %r = sext i32 %a to i64
  ret i64 %r
}
