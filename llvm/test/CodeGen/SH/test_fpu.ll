; RUN: llc -mtriple=sh4-unknown-linux-gnu < %s | FileCheck %s

; CHECK-LABEL: test_fadd:
; CHECK: fadd
define float @test_fadd(float %a, float %b) {
entry:
  %add = fadd float %a, %b
  ret float %add
}

; CHECK-LABEL: test_fsub:
; CHECK: fsub
define float @test_fsub(float %a, float %b) {
entry:
  %sub = fsub float %a, %b
  ret float %sub
}

; CHECK-LABEL: test_fmul:
; CHECK: fmul
define float @test_fmul(float %a, float %b) {
entry:
  %mul = fmul float %a, %b
  ret float %mul
}

; CHECK-LABEL: test_fdiv:
; CHECK: fdiv
define float @test_fdiv(float %a, float %b) {
entry:
  %div = fdiv float %a, %b
  ret float %div
}

; CHECK-LABEL: test_fneg:
; CHECK: fneg
define float @test_fneg(float %a) {
entry:
  %neg = fneg float %a
  ret float %neg
}

; CHECK-LABEL: test_fabs:
; CHECK: fabs
declare float @llvm.fabs.f32(float)
define float @test_fabs(float %a) {
entry:
  %abs = call float @llvm.fabs.f32(float %a)
  ret float %abs
}

; CHECK-LABEL: test_fsqrt:
; CHECK: fsqrt
declare float @llvm.sqrt.f32(float)
define float @test_fsqrt(float %a) {
entry:
  %sqrt = call float @llvm.sqrt.f32(float %a)
  ret float %sqrt
}

; CHECK-LABEL: test_fadd_d:
; CHECK: fadd dr
define double @test_fadd_d(double %a, double %b) {
entry:
  %add = fadd double %a, %b
  ret double %add
}

; CHECK-LABEL: test_fsub_d:
; CHECK: fsub dr
define double @test_fsub_d(double %a, double %b) {
entry:
  %sub = fsub double %a, %b
  ret double %sub
}

; CHECK-LABEL: test_fmul_d:
; CHECK: fmul dr
define double @test_fmul_d(double %a, double %b) {
entry:
  %mul = fmul double %a, %b
  ret double %mul
}

; CHECK-LABEL: test_fdiv_d:
; CHECK: fdiv dr
define double @test_fdiv_d(double %a, double %b) {
entry:
  %div = fdiv double %a, %b
  ret double %div
}
