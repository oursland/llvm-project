; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test constant materialization patterns.

; CHECK-LABEL: const_small:
; Small constants use MOV ##imm8.
; CHECK: mov	##42, r0
define i32 @const_small() {
  ret i32 42
}

; CHECK-LABEL: const_neg:
; CHECK: mov	##-1, r0
define i32 @const_neg() {
  ret i32 -1
}

; CHECK-LABEL: const_zero:
; CHECK: mov	##0, r0
define i32 @const_zero() {
  ret i32 0
}

; CHECK-LABEL: const_large:
; Large constants use constant pool (mov.l @(disp,pc), Rn).
; CHECK: mov.l	@(.LCPI{{[0-9]+}}_0, pc), r0
define i32 @const_large() {
  ret i32 305419896 ; 0x12345678
}

; CHECK-LABEL: const_global:
; Global address materialization.
; CHECK: mov.l	@(.LCPI{{[0-9]+}}_0, pc), r
define ptr @const_global() {
  ret ptr @global_var
}

; CHECK-LABEL: trap:
; CHECK: trapa	##0
define void @trap() {
  call void @llvm.trap()
  unreachable
}

@global_var = external global i32
declare void @llvm.trap()
