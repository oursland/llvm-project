; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test inline assembly support.

; CHECK-LABEL: asm_nop:
; CHECK: nop
define void @asm_nop() {
  call void asm sideeffect "nop", ""()
  ret void
}

; CHECK-LABEL: asm_mov:
; CHECK: mov	r4, r0
define i32 @asm_mov(i32 %a) {
  %r = call i32 asm "mov $1, $0", "=r,r"(i32 %a)
  ret i32 %r
}

; CHECK-LABEL: asm_memory:
; Inline asm with memory constraint.
define void @asm_memory(ptr %p) {
  call void asm sideeffect "mov.l @$0, r0", "r,~{r0}"(ptr %p)
  ret void
}
