; RUN: llc -mtriple=sh2-unknown-linux-gnu  -O2 < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  -O2 < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  -O2 < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu -O2 < %s | FileCheck %s

; Test DT (Decrement and Test) loop optimization.
; DT Rn: Rn -= 1; T = (Rn == 0)
; Replaces ADD #-1 + CMP/EQ #0 sequences in countdown loops.

; CHECK-LABEL: simple_countdown:
; CHECK:       .LBB0_1:
; CHECK:         dt r4
; CHECK-NEXT:    bf .LBB0_1
define void @simple_countdown(i32 %n, ptr %p) {
entry:
  br label %loop

loop:
  %i = phi i32 [ %n, %entry ], [ %dec, %loop ]
  store volatile i32 %i, ptr %p
  %dec = add i32 %i, -1
  %cond = icmp ne i32 %dec, 0
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}

; Test that DT works with a non-volatile store (different scheduling).
; CHECK-LABEL: countdown_store:
; CHECK:         dt r{{[0-9]+}}
; CHECK-NEXT:    bf
define void @countdown_store(i32 %n, ptr %p) {
entry:
  br label %loop

loop:
  %i = phi i32 [ %n, %entry ], [ %dec, %loop ]
  store volatile i32 %i, ptr %p
  %dec = add i32 %i, -1
  %cond = icmp ne i32 %dec, 0
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}

; Verify that non-countdown loops (comparing against non-zero) are NOT
; converted to DT.
; CHECK-LABEL: not_countdown:
; CHECK-NOT:     dt
; CHECK:         cmp/eq
define void @not_countdown(i32 %n, i32 %limit) {
entry:
  br label %loop

loop:
  %i = phi i32 [ %n, %entry ], [ %dec, %loop ]
  %dec = add i32 %i, -1
  %cond = icmp ne i32 %dec, %limit
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}
