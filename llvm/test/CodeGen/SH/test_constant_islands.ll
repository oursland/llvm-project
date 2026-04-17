; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

@g1 = external global i32
@g2 = external global i32
@g3 = external global i32

; Should generate a constant pool load close to the instruction.
; CHECK-LABEL: test_simple_cp:
; CHECK:       mov.l @([[CPI:\.LCPI[0-9]+_[0-9]+]], pc), r0
; CHECK:       mov.l @r0, r0
; CHECK:       rts
; CHECK-NEXT:  nop
; CHECK:       [[CPI]]:
; CHECK-NEXT:  .long g1
define i32 @test_simple_cp() {
entry:
  %0 = load i32, ptr @g1, align 4
  ret i32 %0
}

; Should generate multiple constant pool loads, and the constant island pass
; must place the pool entries within reach (8-bit * 4 for MOVL).
; CHECK-LABEL: test_multiple_cp:
; CHECK:       mov.l @([[CPI1:\.LCPI[0-9]+_[0-9]+]], pc), r0
; CHECK:       mov.l @r0, r1
; CHECK:       mov.l @([[CPI2:\.LCPI[0-9]+_[0-9]+]], pc), r0
; CHECK:       mov.l @r0, r0
; CHECK:       add r1, r0
; CHECK:       mov.l @([[CPI3:\.LCPI[0-9]+_[0-9]+]], pc), r1
; CHECK:       mov.l @r1, r1
; CHECK:       add r1, r0
; CHECK:       rts
; CHECK-NEXT:  nop
; CHECK:       [[CPI1]]:
; CHECK-NEXT:  .long g2
; CHECK:       [[CPI2]]:
; CHECK-NEXT:  .long g1
; CHECK:       [[CPI3]]:
; CHECK-NEXT:  .long g3
define i32 @test_multiple_cp() {
entry:
  %0 = load i32, ptr @g1, align 4
  %1 = load i32, ptr @g2, align 4
  %2 = load i32, ptr @g3, align 4
  %add = add nsw i32 %0, %1
  %add1 = add nsw i32 %add, %2
  ret i32 %add1
}
