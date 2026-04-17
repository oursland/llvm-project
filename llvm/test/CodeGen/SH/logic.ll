; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; CHECK-LABEL: and_reg:
; CHECK: and	r5, r0
define i32 @and_reg(i32 %a, i32 %b) {
  %r = and i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: or_reg:
; CHECK: or	r5, r0
define i32 @or_reg(i32 %a, i32 %b) {
  %r = or i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: xor_reg:
; CHECK: xor	r5, r0
define i32 @xor_reg(i32 %a, i32 %b) {
  %r = xor i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: not_reg:
; CHECK: not	r4, r0
define i32 @not_reg(i32 %a) {
  %r = xor i32 %a, -1
  ret i32 %r
}
