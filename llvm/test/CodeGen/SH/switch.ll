; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test switch statement lowering (jump table or cascade).

; CHECK-LABEL: switch_small:
define i32 @switch_small(i32 %x) {
entry:
  switch i32 %x, label %default [
    i32 0, label %case0
    i32 1, label %case1
    i32 2, label %case2
  ]
case0:
  ret i32 10
case1:
  ret i32 20
case2:
  ret i32 30
default:
  ret i32 -1
}

; CHECK-LABEL: switch_large:
; Large switch should use a jump table or similar.
define i32 @switch_large(i32 %x) {
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
    i32 5, label %c5
    i32 6, label %c6
    i32 7, label %c7
  ]
c0: ret i32 100
c1: ret i32 101
c2: ret i32 102
c3: ret i32 103
c4: ret i32 104
c5: ret i32 105
c6: ret i32 106
c7: ret i32 107
default: ret i32 -1
}
