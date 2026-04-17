; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test calling convention: calls, returns, argument passing.

; CHECK-LABEL: call_void:
; CHECK: jsr	@r{{[0-9]+}}
define void @call_void() {
  call void @external_void()
  ret void
}

; CHECK-LABEL: call_ret_i32:
; CHECK: jsr
define i32 @call_ret_i32() {
  %r = call i32 @external_i32()
  ret i32 %r
}

; CHECK-LABEL: call_args_4:
; R4-R7 used for first 4 i32 args
; CHECK: mov	##1, r4
; CHECK: jsr
define i32 @call_args_4() {
  %r = call i32 @external_4args(i32 1, i32 2, i32 3, i32 4)
  ret i32 %r
}

; CHECK-LABEL: call_args_6:
; Last 2 args go on the stack
; CHECK: mov.l	r{{[0-9]+}}, @r15
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

; CHECK-LABEL: ret_i64:
; i64 returned in R0:R1
define i64 @ret_i64() {
  ret i64 1234567890123
}

; CHECK-LABEL: ret_float:
; f32 returned in FR0
define float @ret_float() {
  ret float 1.0
}

; CHECK-LABEL: ret_double:
; f64 returned in DR0
define double @ret_double() {
  ret double 2.0
}

; CHECK-LABEL: args_float:
; f32 args: result in FR0 after arithmetic
; CHECK: fadd
define float @args_float(float %a, float %b) {
  %r = fadd float %a, %b
  ret float %r
}

declare void @external_void()
declare i32 @external_i32()
declare i32 @external_4args(i32, i32, i32, i32)
declare i32 @external_4args_plus_2(i32, i32, i32, i32, i32, i32)
