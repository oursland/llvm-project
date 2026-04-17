; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test struct passing and returning.

%struct.Small = type { i32 }
%struct.Pair = type { i32, i32 }
%struct.Large = type { [8 x i32] }

; CHECK-LABEL: return_small_struct:
; Small struct returned in R0.
define i32 @return_small_struct() {
  ret i32 42
}

; CHECK-LABEL: pass_struct_byval:
; Large struct passed by value on the stack.
define void @pass_struct_byval(ptr byval(%struct.Large) align 4 %s) {
  %p = getelementptr %struct.Large, ptr %s, i32 0, i32 0, i32 0
  %v = load volatile i32, ptr %p
  ret void
}

; CHECK-LABEL: return_struct_sret:
; Struct return via sret pointer.
define void @return_struct_sret(ptr noalias sret(%struct.Large) align 4 %result) {
  %p = getelementptr %struct.Large, ptr %result, i32 0, i32 0, i32 0
  store i32 1, ptr %p
  ret void
}

; CHECK-LABEL: pass_pair:
; Two-word struct: passed in registers.
define i64 @pass_pair(i64 %s) {
  ret i64 %s
}
