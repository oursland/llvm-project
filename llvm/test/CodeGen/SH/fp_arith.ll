; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test FPU arithmetic patterns.

; CHECK-LABEL: fadd_f32:
; CHECK: fadd	fr4, fr0
define float @fadd_f32(float %a, float %b) {
  %r = fadd float %a, %b
  ret float %r
}

; CHECK-LABEL: fsub_f32:
; CHECK: fsub	fr4, fr0
define float @fsub_f32(float %a, float %b) {
  %r = fsub float %a, %b
  ret float %r
}

; CHECK-LABEL: fmul_f32:
; CHECK: fmul	fr4, fr0
define float @fmul_f32(float %a, float %b) {
  %r = fmul float %a, %b
  ret float %r
}

; CHECK-LABEL: fdiv_f32:
; CHECK: fdiv	fr4, fr0
define float @fdiv_f32(float %a, float %b) {
  %r = fdiv float %a, %b
  ret float %r
}

; CHECK-LABEL: fneg_f32:
; CHECK: fneg	fr0
define float @fneg_f32(float %a) {
  %r = fneg float %a
  ret float %r
}

; CHECK-LABEL: fabs_f32:
; CHECK: fabs	fr0
define float @fabs_f32(float %a) {
  %r = call float @llvm.fabs.f32(float %a)
  ret float %r
}

; CHECK-LABEL: fsqrt_f32:
; CHECK: fsqrt	fr0
define float @fsqrt_f32(float %a) {
  %r = call float @llvm.sqrt.f32(float %a)
  ret float %r
}

; CHECK-LABEL: fadd_f64:
; CHECK: fadd	dr{{[0-9]+}}, dr0
define double @fadd_f64(double %a, double %b) {
  %r = fadd double %a, %b
  ret double %r
}

; CHECK-LABEL: sitofp_i32_f32:
; CHECK: float	fpul, fr0
define float @sitofp_i32_f32(i32 %a) {
  %r = sitofp i32 %a to float
  ret float %r
}

; CHECK-LABEL: fptosi_f32_i32:
; CHECK: ftrc	fr{{[0-9]+}}, fpul
define i32 @fptosi_f32_i32(float %a) {
  %r = fptosi float %a to i32
  ret i32 %r
}

; CHECK-LABEL: fpext_f32_f64:
; CHECK: fcnvsd	fpul, dr
define double @fpext_f32_f64(float %a) {
  %r = fpext float %a to double
  ret double %r
}

; CHECK-LABEL: fptrunc_f64_f32:
; CHECK: fcnvds	dr{{[0-9]+}}, fpul
define float @fptrunc_f64_f32(double %a) {
  %r = fptrunc double %a to float
  ret float %r
}

; CHECK-LABEL: fcmp_oeq:
; CHECK: fcmp/eq	fr4, fr5
define i32 @fcmp_oeq(float %a, float %b) {
  %c = fcmp oeq float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: fcmp_ogt:
; CHECK: fcmp/gt	fr4, fr5
define i32 @fcmp_ogt(float %a, float %b) {
  %c = fcmp ogt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

declare float @llvm.fabs.f32(float)
declare float @llvm.sqrt.f32(float)
