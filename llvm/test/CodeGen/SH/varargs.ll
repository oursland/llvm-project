; RUN: llc -mtriple=sh4-unknown-linux-gnu < %s | FileCheck %s

; Test variadic function support.

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

; CHECK-LABEL: varargs_callee:
; Variadic callee must save register args to stack for va_list traversal.
define i32 @varargs_callee(i32 %count, ...) {
entry:
  %ap = alloca ptr, align 4
  call void @llvm.va_start(ptr %ap)
  %ap.val = load ptr, ptr %ap, align 4
  %arg1 = load i32, ptr %ap.val, align 4
  call void @llvm.va_end(ptr %ap)
  ret i32 %arg1
}

; CHECK-LABEL: varargs_caller:
; Calling a variadic function.
; CHECK: jsr
define i32 @varargs_caller() {
  %r = call i32 (i32, ...) @varargs_callee(i32 1, i32 2, i32 3)
  ret i32 %r
}

; CHECK-LABEL: varargs_many:
; More than 4 args.
; CHECK: jsr
define i32 @varargs_many() {
  %r = call i32 (i32, ...) @varargs_callee(i32 1, i32 2, i32 3, i32 4, i32 5)
  ret i32 %r
}
