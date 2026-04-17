; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test dynamic stack allocation (alloca).

; CHECK-LABEL: dynamic_alloca:
; Dynamic alloca needs to adjust SP at runtime.
; CHECK: sub	r
define void @dynamic_alloca(i32 %n) {
  %p = alloca i32, i32 %n
  store volatile i32 0, ptr %p
  ret void
}

; CHECK-LABEL: alloca_with_call:
; Alloca combined with a function call.
define i32 @alloca_with_call(i32 %n) {
  %p = alloca i32, i32 %n
  store i32 42, ptr %p
  %r = call i32 @use_ptr(ptr %p)
  ret i32 %r
}

declare i32 @use_ptr(ptr)
