; RUN: llc -mtriple=sh2-unknown-linux-gnu  -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu -relocation-model=pic < %s | FileCheck %s

; CHECK-LABEL: test_pic:
; CHECK: mova
; CHECK: mov.l
@var = external global i32, align 4

define i32 @test_pic() {
entry:
  %0 = load i32, ptr @var, align 4
  ret i32 %0
}
