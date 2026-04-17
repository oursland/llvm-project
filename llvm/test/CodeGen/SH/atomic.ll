; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s

; Test atomic operations. SH uses libcalls for atomic operations on every
; variant we support today, including sh4a -- native LL/SC lowering
; (movli.l / movco.l) is not wired up yet.

; CHECK-LABEL: atomic_load_i32:
; Atomic load uses __atomic_load_4 libcall.
; CHECK: __atomic_load_4
define i32 @atomic_load_i32(ptr %p) {
  %v = load atomic i32, ptr %p seq_cst, align 4
  ret i32 %v
}

; CHECK-LABEL: atomic_store_i32:
; Atomic store uses __atomic_store_4 libcall.
; CHECK: __atomic_store_4
define void @atomic_store_i32(ptr %p, i32 %v) {
  store atomic i32 %v, ptr %p seq_cst, align 4
  ret void
}

; CHECK-LABEL: fence_seq_cst:
; CHECK: rts
define void @fence_seq_cst() {
  fence seq_cst
  ret void
}
