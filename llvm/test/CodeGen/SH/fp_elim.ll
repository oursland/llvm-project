; RUN: llc -mtriple=sh2-unknown-linux-gnu  -frame-pointer=none < %s | FileCheck --check-prefix=CHECK-NOFP %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  -frame-pointer=none < %s | FileCheck --check-prefix=CHECK-NOFP %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  -frame-pointer=none < %s | FileCheck --check-prefix=CHECK-NOFP %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu -frame-pointer=none < %s | FileCheck --check-prefix=CHECK-NOFP %s
; RUN: llc -mtriple=sh2-unknown-linux-gnu  -frame-pointer=all < %s | FileCheck --check-prefix=CHECK-FP %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  -frame-pointer=all < %s | FileCheck --check-prefix=CHECK-FP %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  -frame-pointer=all < %s | FileCheck --check-prefix=CHECK-FP %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu -frame-pointer=all < %s | FileCheck --check-prefix=CHECK-FP %s

; Test frame pointer elimination: when frame pointer is not requested,
; simple functions should not push/pop R14.

; CHECK-NOFP-LABEL: simple_leaf:
; CHECK-NOFP-NOT: r14
; CHECK-NOFP: rts

; CHECK-FP-LABEL: simple_leaf:
; CHECK-FP: mov.l	r14, @-r15
define i32 @simple_leaf(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

; Functions with calls should still omit FP when not requested.
; CHECK-NOFP-LABEL: with_call:
; CHECK-NOFP-NOT: mov{{.*}}r15, r14
; CHECK-NOFP: jsr
define i32 @with_call(i32 %a) {
  %r = call i32 @external(i32 %a)
  ret i32 %r
}

declare i32 @external(i32)
