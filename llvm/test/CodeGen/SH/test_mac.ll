; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; CHECK-LABEL: test_mul:
; CHECK: mul.l
; CHECK: sts macl
define i32 @test_mul(i32 %a, i32 %b) {
entry:
  %res = mul i32 %a, %b
  ret i32 %res
}

; CHECK-LABEL: test_macl:
; CHECK: mac.l
define void @test_macl(ptr %p1, ptr %p2) {
entry:
  call void asm sideeffect "mac.l @$0+, @$1+", "r,r,~{mach},~{macl}"(ptr %p1, ptr %p2)
  ret void
}

; CHECK-LABEL: test_macw:
; CHECK: mac.w
define void @test_macw(ptr %p1, ptr %p2) {
entry:
  call void asm sideeffect "mac.w @$0+, @$1+", "r,r,~{mach},~{macl}"(ptr %p1, ptr %p2)
  ret void
}

; CHECK-LABEL: test_clrmac:
; CHECK: clrmac
define void @test_clrmac() {
entry:
  call void asm sideeffect "clrmac", "~{mach},~{macl}"()
  ret void
}
