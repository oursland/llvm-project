; RUN: llc -mtriple=sh2-unknown-linux-gnu  -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu -relocation-model=pic < %s | FileCheck %s

; CHECK-LABEL: test_call:
; CHECK: jsr
declare void @external_func()

define void @test_call() {
entry:
  call void @external_func()
  ret void
}
