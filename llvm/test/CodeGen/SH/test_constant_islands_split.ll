; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

@g1 = external global i32

; To force a block split, we create a very long basic block where the
; distance from the PC-relative load to the end of the block exceeds the
; maximum displacement. SH MOVL_PCREL displacement is 8 bits scaled by 4,
; so the maximum is 255 * 4 = 1020 bytes.
; A single NOP is 2 bytes, so we need > 510 NOP-equivalent instructions.

; CHECK-LABEL: test_large_bb:
; CHECK:       mov.l @([[CPI:\.LCPI[0-9]+_[0-9]+]], pc), r0
; CHECK:       mov.l @r0, r0
; CHECK:       bra [[DEST:\.LBB[0-9]+_[0-9]+]]
; CHECK-NEXT:  nop
; CHECK:       [[CPI]]:
; CHECK-NEXT:  .long g1
; CHECK:       [[DEST]]:
; CHECK:       rts

define i32 @test_large_bb() {
entry:
  %0 = load i32, ptr @g1, align 4
  
  ; Manually unroll a lot of instructions or use a macro...
  ; Here we'll just insert a massive inline assembly black box
  ; that the compiler thinks takes a lot of bytes (e.g., 2000 bytes).
  
  ; This assumes the target respects the inline asm length or we can 
  ; just use `unreachable` or multiple adds.
  
  call void asm sideeffect ".space 2000", ""()
  ret i32 %0
}
