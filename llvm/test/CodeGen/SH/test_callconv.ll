; RUN: llc -mtriple=sh4-unknown-linux-gnu < %s | FileCheck %s
%struct.Large = type { [4 x i32] }

; Verify that args beyond R4-R7 come from the stack area.
; CHECK-LABEL: test_stack_args:
; CHECK: add
define i32 @test_stack_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f) {
entry:
  %sum1 = add i32 %a, %b
  %sum2 = add i32 %sum1, %c
  %sum3 = add i32 %sum2, %d
  %sum4 = add i32 %sum3, %e
  %sum5 = add i32 %sum4, %f
  ret i32 %sum5
}

; CHECK-LABEL: test_byval:
; CHECK: rts
define void @test_byval(ptr byval(%struct.Large) align 4 %s) {
entry:
  %p = getelementptr inbounds %struct.Large, ptr %s, i32 0, i32 0, i32 0
  %val = load i32, ptr %p, align 4
  ret void
}

; CHECK-LABEL: test_sret:
; CHECK: mov {{.*}}##42
define void @test_sret(ptr noalias sret(%struct.Large) align 4 %agg.result) {
entry:
  %p = getelementptr inbounds %struct.Large, ptr %agg.result, i32 0, i32 0, i32 0
  store i32 42, ptr %p, align 4
  ret void
}

; CHECK-LABEL: call_byval:
; CHECK: jsr
define void @call_byval(ptr %s) {
entry:
  call void @test_byval(ptr byval(%struct.Large) align 4 %s)
  ret void
}

; CHECK-LABEL: call_sret:
; CHECK: jsr
define void @call_sret(ptr %s) {
entry:
  call void @test_sret(ptr sret(%struct.Large) align 4 %s)
  ret void
}

declare void @llvm.va_start(ptr)

; CHECK-LABEL: test_varargs:
; CHECK: mov.l
define i32 @test_varargs(i32 %count, ...) {
entry:
  %ap = alloca ptr, align 4
  call void @llvm.va_start(ptr %ap)
  %ap.val = load ptr, ptr %ap, align 4

  ; Load the first vararg
  %arg1 = load i32, ptr %ap.val, align 4

  ; Advance the pointer
  %ap.next = getelementptr i32, ptr %ap.val, i32 1
  store ptr %ap.next, ptr %ap, align 4

  ret i32 %arg1
}
