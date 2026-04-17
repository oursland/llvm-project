; RUN: llc -mtriple=sh2-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu < %s | FileCheck %s

; Soft-float companion of fp_arith.ll: on sh2/sh3 the backend has no FPU, so
; arithmetic, conversions, and compares all lower to libcalls. fneg/fabs are
; lowered inline as bitmask ops on the i32 view of the float.

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

; CHECK-LABEL: fneg_f32:
; Sign-bit flip with 0x80000000.
; CHECK: xor
define float @fneg_f32(float %a) {
  %r = fneg float %a
  ret float %r
}

; CHECK-LABEL: fabs_f32:
; Mask off the sign bit with 0x7fffffff.
; CHECK: and
define float @fabs_f32(float %a) {
  %r = call float @llvm.fabs.f32(float %a)
  ret float %r
}

; CHECK-LABEL: fsqrt_f32:
; Lowered through the libm sqrtf, not a compiler-rt helper.
; CHECK: sqrtf
define float @fsqrt_f32(float %a) {
  %r = call float @llvm.sqrt.f32(float %a)
  ret float %r
}

; CHECK-LABEL: fadd_f64:
; CHECK: __adddf3
define double @fadd_f64(double %a, double %b) {
  %r = fadd double %a, %b
  ret double %r
}

; CHECK-LABEL: sitofp_i32_f32:
; CHECK: __floatsisf
define float @sitofp_i32_f32(i32 %a) {
  %r = sitofp i32 %a to float
  ret float %r
}

; CHECK-LABEL: fptosi_f32_i32:
; CHECK: __fixsfsi
define i32 @fptosi_f32_i32(float %a) {
  %r = fptosi float %a to i32
  ret i32 %r
}

; CHECK-LABEL: fpext_f32_f64:
; CHECK: __extendsfdf2
define double @fpext_f32_f64(float %a) {
  %r = fpext float %a to double
  ret double %r
}

; CHECK-LABEL: fptrunc_f64_f32:
; CHECK: __truncdfsf2
define float @fptrunc_f64_f32(double %a) {
  %r = fptrunc double %a to float
  ret float %r
}

; CHECK-LABEL: fcmp_oeq:
; CHECK: __eqsf2
define i32 @fcmp_oeq(float %a, float %b) {
  %c = fcmp oeq float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: fcmp_ogt:
; CHECK: __gtsf2
define i32 @fcmp_ogt(float %a, float %b) {
  %c = fcmp ogt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

declare float @llvm.fabs.f32(float)
declare float @llvm.sqrt.f32(float)
