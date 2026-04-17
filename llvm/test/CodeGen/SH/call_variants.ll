; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Integer-only subset of call.ll that is expected to codegen identically
; across all SuperH ISA variants (sh2 / sh3 / sh4 / sh4a). FP call paths
; live in call.ll, which requires an FPU target.

declare void @external_void()
declare i32 @external_4args(i32, i32, i32, i32)
declare i32 @external_4args_plus_2(i32, i32, i32, i32, i32, i32)

; CHECK-LABEL: call_void:
; CHECK: jsr	@r{{[0-9]+}}
define void @call_void() {
  call void @external_void()
  ret void
}

; CHECK-LABEL: call_args_4:
; R4-R7 used for first 4 i32 args.
; CHECK: mov	##1, r4
; CHECK: mov	##2, r5
; CHECK: mov	##3, r6
; CHECK: mov	##4, r7
; CHECK: jsr
define i32 @call_args_4() {
  %r = call i32 @external_4args(i32 1, i32 2, i32 3, i32 4)
  ret i32 %r
}

; CHECK-LABEL: call_args_6:
; Args 5-6 spill to the stack.
; CHECK: mov.l	r{{[0-9]+}}, @r15
; CHECK: jsr
define i32 @call_args_6() {
  %r = call i32 @external_4args_plus_2(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6)
  ret i32 %r
}

; CHECK-LABEL: call_indirect:
; CHECK: jsr	@r4
define i32 @call_indirect(ptr %fp) {
  %r = call i32 %fp()
  ret i32 %r
}
