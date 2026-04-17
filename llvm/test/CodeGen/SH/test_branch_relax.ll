; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Verify branch relaxation: the large inline asm block forces the
; conditional branch to exceed 8-bit displacement range.
; CHECK-LABEL: test_branch_relax:
; CHECK: cmp/eq
; CHECK: bra
define i32 @test_branch_relax(i32 %a, i32 %b) {
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %if.then, label %if.else

if.then:
  call void asm sideeffect "\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop\0Anop", ""()
  ret i32 1

if.else:
  ret i32 0
}
