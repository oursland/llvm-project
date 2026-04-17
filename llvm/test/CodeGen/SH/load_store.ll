; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck --check-prefixes=CHECK,NOFPU %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck --check-prefixes=CHECK,NOFPU %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck --check-prefixes=CHECK,FPU %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck --check-prefixes=CHECK,FPU %s

; CHECK-LABEL: load_i32:
; CHECK: mov.l	@r4, r0
define i32 @load_i32(ptr %p) {
  %v = load i32, ptr %p
  ret i32 %v
}

; CHECK-LABEL: load_i16_sext:
; CHECK: mov.w	@r4, r0
define i32 @load_i16_sext(ptr %p) {
  %v = load i16, ptr %p
  %e = sext i16 %v to i32
  ret i32 %e
}

; CHECK-LABEL: load_i16_zext:
; CHECK: mov.w	@r4, r0
; CHECK: extu.w	r0, r0
define i32 @load_i16_zext(ptr %p) {
  %v = load i16, ptr %p
  %e = zext i16 %v to i32
  ret i32 %e
}

; CHECK-LABEL: load_i8_sext:
; CHECK: mov.b	@r4, r0
define i32 @load_i8_sext(ptr %p) {
  %v = load i8, ptr %p
  %e = sext i8 %v to i32
  ret i32 %e
}

; CHECK-LABEL: load_i8_zext:
; CHECK: mov.b	@r4, r0
; CHECK: extu.b	r0, r0
define i32 @load_i8_zext(ptr %p) {
  %v = load i8, ptr %p
  %e = zext i8 %v to i32
  ret i32 %e
}

; CHECK-LABEL: store_i32:
; CHECK: mov.l	r5, @r4
define void @store_i32(ptr %p, i32 %v) {
  store i32 %v, ptr %p
  ret void
}

; CHECK-LABEL: store_i16:
; CHECK: mov.w	r5, @r4
define void @store_i16(ptr %p, i16 %v) {
  store i16 %v, ptr %p
  ret void
}

; CHECK-LABEL: store_i8:
; CHECK: mov.b	r5, @r4
define void @store_i8(ptr %p, i8 %v) {
  store i8 %v, ptr %p
  ret void
}

; CHECK-LABEL: load_f32:
; FPU:   fmov.s	@r4, fr0
; NOFPU: mov.l	@r4, r0
define float @load_f32(ptr %p) {
  %v = load float, ptr %p
  ret float %v
}

; CHECK-LABEL: store_f32:
; FPU:   fmov.s	fr5, @r4
; NOFPU: mov.l	r5, @r4
define void @store_f32(ptr %p, float %v) {
  store float %v, ptr %p
  ret void
}
