; RUN: llc -mtriple=sh2-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu < %s | FileCheck %s

; SH2 and SH3 have no FPU, so all float/double arithmetic must be lowered to
; soft-float libcalls. Complements test_fpu.ll which covers the FPU-present
; path on sh4.

; CHECK-LABEL: fadd_f32:
; CHECK: __addsf3
define float @fadd_f32(float %a, float %b) {
  %r = fadd float %a, %b
  ret float %r
}

; CHECK-LABEL: fsub_f32:
; CHECK: __subsf3
define float @fsub_f32(float %a, float %b) {
  %r = fsub float %a, %b
  ret float %r
}

; CHECK-LABEL: fmul_f32:
; CHECK: __mulsf3
define float @fmul_f32(float %a, float %b) {
  %r = fmul float %a, %b
  ret float %r
}

; CHECK-LABEL: fdiv_f32:
; CHECK: __divsf3
define float @fdiv_f32(float %a, float %b) {
  %r = fdiv float %a, %b
  ret float %r
}

; CHECK-LABEL: fadd_f64:
; CHECK: __adddf3
define double @fadd_f64(double %a, double %b) {
  %r = fadd double %a, %b
  ret double %r
}

; CHECK-LABEL: fmul_f64:
; CHECK: __muldf3
define double @fmul_f64(double %a, double %b) {
  %r = fmul double %a, %b
  ret double %r
}
