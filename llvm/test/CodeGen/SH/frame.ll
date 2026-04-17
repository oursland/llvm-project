; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test frame/prologue/epilogue patterns.

; CHECK-LABEL: leaf_func:
; A simple leaf function.
; CHECK: mov	r4, r0
; CHECK: rts
define i32 @leaf_func(i32 %a) {
  ret i32 %a
}

; CHECK-LABEL: callee_save:
; Function that uses callee-saved registers should save/restore them.
; CHECK: sts.l	pr, @-r15
define i32 @callee_save(i32 %a, i32 %b, i32 %c, i32 %d) {
  %v1 = call i32 @external(i32 %a)
  %v2 = call i32 @external(i32 %b)
  %v3 = call i32 @external(i32 %c)
  %v4 = call i32 @external(i32 %d)
  %s1 = add i32 %v1, %v2
  %s2 = add i32 %s1, %v3
  %s3 = add i32 %s2, %v4
  ret i32 %s3
}

; CHECK-LABEL: local_vars:
; Function with local variables needs stack space.
; CHECK: add	##{{-[0-9]+}}, r15
define i32 @local_vars(i32 %n) {
  %arr = alloca [16 x i32]
  %p = getelementptr [16 x i32], ptr %arr, i32 0, i32 0
  store i32 %n, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

; CHECK-LABEL: large_frame:
; Large stack frames need more than add ##imm8.
define void @large_frame() {
  %arr = alloca [1024 x i32]
  %p = getelementptr [1024 x i32], ptr %arr, i32 0, i32 0
  store volatile i32 0, ptr %p
  ret void
}

declare i32 @external(i32)
