; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test sign/zero extension patterns.

; CHECK-LABEL: sext_i8:
; CHECK: exts.b	r4, r0
define i32 @sext_i8(i32 %a) {
  %t = trunc i32 %a to i8
  %r = sext i8 %t to i32
  ret i32 %r
}

; CHECK-LABEL: sext_i16:
; CHECK: exts.w	r4, r0
define i32 @sext_i16(i32 %a) {
  %t = trunc i32 %a to i16
  %r = sext i16 %t to i32
  ret i32 %r
}

; CHECK-LABEL: zext_i8:
; Truncate to i8 and zero-extend back — uses AND with 0xFF mask.
; CHECK: and
define i32 @zext_i8(i32 %a) {
  %t = trunc i32 %a to i8
  %r = zext i8 %t to i32
  ret i32 %r
}

; CHECK-LABEL: zext_i16:
; Truncate to i16 and zero-extend back — uses AND with 0xFFFF mask.
; CHECK: and
define i32 @zext_i16(i32 %a) {
  %t = trunc i32 %a to i16
  %r = zext i16 %t to i32
  ret i32 %r
}
