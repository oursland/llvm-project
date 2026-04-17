; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test global variable access (static, non-PIC).

@gvar_i32 = global i32 42
@gvar_extern = external global i32

; CHECK-LABEL: load_global:
; CHECK: mov.l	@(.LCPI{{[0-9]+}}_0, pc), r{{[0-9]+}}
; CHECK: mov.l	@r
define i32 @load_global() {
  %v = load i32, ptr @gvar_i32
  ret i32 %v
}

; CHECK-LABEL: store_global:
; CHECK: mov.l	@(.LCPI{{[0-9]+}}_0, pc), r
define void @store_global(i32 %v) {
  store i32 %v, ptr @gvar_i32
  ret void
}

; CHECK-LABEL: addr_global:
; CHECK: mov.l	@(.LCPI{{[0-9]+}}_0, pc), r0
define ptr @addr_global() {
  ret ptr @gvar_extern
}
