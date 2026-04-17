; RUN: llc -mtriple=sh2-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh3-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4-unknown-linux-gnu  < %s | FileCheck %s
; RUN: llc -mtriple=sh4a-unknown-linux-gnu < %s | FileCheck %s
target datalayout = "e-m:e-p:32:32-i64:64-a:0:32-n32-S64"
target triple = "sh4-unknown-linux-gnu"

@_ZTIi = external constant ptr

declare void @throw_exception()
declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: test_eh:
; CHECK: .cfi_startproc
; CHECK: .cfi_personality
; CHECK: jsr
define i32 @test_eh() personality ptr @__gxx_personality_v0 {
entry:
  invoke void @throw_exception()
          to label %try.cont unwind label %lpad

lpad:
  %0 = landingpad { ptr, i32 }
          catch ptr @_ZTIi
  %sel = extractvalue { ptr, i32 } %0, 1
  ret i32 %sel

try.cont:
  ret i32 0
}
