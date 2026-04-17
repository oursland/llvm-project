; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; CHECK-LABEL: test_postinc_walk:
; CHECK: mov.l @r
define i32 @test_postinc_walk(ptr %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %ptr = phi ptr [ %p, %entry ], [ %ptr.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %add, %loop ]
  %i   = phi i32 [ %n, %entry ], [ %i.next, %loop ]

  %val = load i32, ptr %ptr, align 4
  %add = add i32 %sum, %val
  %ptr.next = getelementptr inbounds i32, ptr %ptr, i32 1
  %i.next = add nsw i32 %i, -1
  %cond = icmp sgt i32 %i.next, 0
  br i1 %cond, label %loop, label %exit

exit:
  %res = phi i32 [ 0, %entry ], [ %add, %loop ]
  ret i32 %res
}

; CHECK-LABEL: test_predec_walk:
; CHECK: mov.l
define void @test_predec_walk(ptr %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %ptr = phi ptr [ %p, %entry ], [ %ptr.next, %loop ]
  %i   = phi i32 [ %n, %entry ], [ %i.next, %loop ]

  %ptr.next = getelementptr inbounds i32, ptr %ptr, i32 -1
  store i32 %i, ptr %ptr.next, align 4
  %i.next = add nsw i32 %i, -1
  %cond = icmp sgt i32 %i.next, 0
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}

; CHECK-LABEL: test_r0_indexed_load:
; CHECK: mov.l @(r0
define i32 @test_r0_indexed_load(ptr %p, i32 %offset) {
entry:
  %addr = getelementptr inbounds i32, ptr %p, i32 %offset
  %val = load i32, ptr %addr, align 4
  ret i32 %val
}

; CHECK-LABEL: test_r0_indexed_store:
; CHECK: mov.l
define void @test_r0_indexed_store(ptr %p, i32 %offset, i32 %val) {
entry:
  %addr = getelementptr inbounds i32, ptr %p, i32 %offset
  store i32 %val, ptr %addr, align 4
  ret void
}
