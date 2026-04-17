; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; CHECK-LABEL: shl_1:
; CHECK: shll	r0
define i32 @shl_1(i32 %a) {
  %r = shl i32 %a, 1
  ret i32 %r
}

; CHECK-LABEL: shl_2:
; CHECK: shll2	r0
define i32 @shl_2(i32 %a) {
  %r = shl i32 %a, 2
  ret i32 %r
}

; CHECK-LABEL: shl_8:
; CHECK: shll8	r0
define i32 @shl_8(i32 %a) {
  %r = shl i32 %a, 8
  ret i32 %r
}

; CHECK-LABEL: shl_16:
; CHECK: shll16	r0
define i32 @shl_16(i32 %a) {
  %r = shl i32 %a, 16
  ret i32 %r
}

; CHECK-LABEL: lshr_1:
; CHECK: shlr	r0
define i32 @lshr_1(i32 %a) {
  %r = lshr i32 %a, 1
  ret i32 %r
}

; CHECK-LABEL: lshr_2:
; CHECK: shlr2	r0
define i32 @lshr_2(i32 %a) {
  %r = lshr i32 %a, 2
  ret i32 %r
}

; CHECK-LABEL: lshr_8:
; CHECK: shlr8	r0
define i32 @lshr_8(i32 %a) {
  %r = lshr i32 %a, 8
  ret i32 %r
}

; CHECK-LABEL: lshr_16:
; CHECK: shlr16	r0
define i32 @lshr_16(i32 %a) {
  %r = lshr i32 %a, 16
  ret i32 %r
}

; CHECK-LABEL: ashr_1:
; CHECK: shar	r0
define i32 @ashr_1(i32 %a) {
  %r = ashr i32 %a, 1
  ret i32 %r
}

; CHECK-LABEL: shl_dynamic:
define i32 @shl_dynamic(i32 %a, i32 %b) {
  %r = shl i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: lshr_dynamic:
define i32 @lshr_dynamic(i32 %a, i32 %b) {
  %r = lshr i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: ashr_dynamic:
define i32 @ashr_dynamic(i32 %a, i32 %b) {
  %r = ashr i32 %a, %b
  ret i32 %r
}
