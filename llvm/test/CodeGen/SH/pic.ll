; RUN: llc -mtriple=sh2-unknown-linux-gnu  -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu -relocation-model=pic < %s | FileCheck %s

; Test PIC (position-independent code) patterns.

; CHECK-LABEL: get_global_pic:
; PIC global access uses GOT via mova/const pool.
; CHECK: mova
define i32 @get_global_pic() {
  %v = load i32, ptr @gvar
  ret i32 %v
}

; CHECK-LABEL: call_external_pic:
; PIC function call.
; CHECK: jsr
define i32 @call_external_pic() {
  %r = call i32 @ext_func()
  ret i32 %r
}

; CHECK-LABEL: addr_of_global_pic:
; Taking address of global in PIC mode.
; CHECK: mova
define ptr @addr_of_global_pic() {
  ret ptr @gvar
}

@gvar = external global i32
declare i32 @ext_func()
