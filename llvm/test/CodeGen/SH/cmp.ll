; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test comparison and conditional branch patterns.

; CHECK-LABEL: cmp_eq:
; CHECK: cmp/eq	r5, r4
; CHECK: movt	r0
define i32 @cmp_eq(i32 %a, i32 %b) {
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: cmp_ne:
; CHECK: cmp/eq	r5, r4
define i32 @cmp_ne(i32 %a, i32 %b) {
  %c = icmp ne i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: cmp_sgt:
; CHECK: cmp/gt	r5, r4
define i32 @cmp_sgt(i32 %a, i32 %b) {
  %c = icmp sgt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: cmp_sge:
; CHECK: cmp/ge	r5, r4
define i32 @cmp_sge(i32 %a, i32 %b) {
  %c = icmp sge i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: cmp_ugt:
; CHECK: cmp/hi	r5, r4
define i32 @cmp_ugt(i32 %a, i32 %b) {
  %c = icmp ugt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: cmp_uge:
; CHECK: cmp/hs	r5, r4
define i32 @cmp_uge(i32 %a, i32 %b) {
  %c = icmp uge i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: br_eq:
; CHECK: cmp/eq
; CHECK: {{bt|bf}}
define i32 @br_eq(i32 %a, i32 %b) {
entry:
  %c = icmp eq i32 %a, %b
  br i1 %c, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; CHECK-LABEL: select_i32:
; CHECK: cmp/gt
define i32 @select_i32(i32 %a, i32 %b, i32 %x, i32 %y) {
  %c = icmp sgt i32 %a, %b
  %r = select i1 %c, i32 %x, i32 %y
  ret i32 %r
}
