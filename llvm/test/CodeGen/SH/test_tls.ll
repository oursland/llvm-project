; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; CHECK-LABEL: test_tls:
; CHECK: @TPOFF
@tls_var = thread_local global i32 0, align 4

define i32 @test_tls() {
entry:
  %0 = load i32, ptr @tls_var, align 4
  ret i32 %0
}
