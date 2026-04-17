// RUN: %clang_cc1 -triple sh-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// Test struct passing conventions for various sizes.

// 1-byte struct: passed in register as i8 coerced.
struct S1 { char a; };
// CHECK-LABEL: define{{.*}} void @pass_s1(i32 inreg %s.coerce)
void pass_s1(struct S1 s) {}

// 2-byte struct: passed in register.
struct S2 { short a; };
// CHECK-LABEL: define{{.*}} void @pass_s2(i32 inreg %s.coerce)
void pass_s2(struct S2 s) {}

// 3-byte struct:
struct S3 { char a; short b; };
// CHECK-LABEL: define{{.*}} void @pass_s3(i32 inreg %s.coerce)
void pass_s3(struct S3 s) {}

// 4-byte struct: one register.
struct S4 { int a; };
// CHECK-LABEL: define{{.*}} void @pass_s4(i32 inreg %s.coerce)
void pass_s4(struct S4 s) {}

// 8-byte struct: two registers.
struct S8 { int a; int b; };
// CHECK-LABEL: define{{.*}} void @pass_s8(i32 inreg %{{.*}}, i32 inreg %{{.*}})
void pass_s8(struct S8 s) {}

// 12-byte struct: three registers.
struct S12 { int a; int b; int c; };
// CHECK-LABEL: define{{.*}} void @pass_s12(i32 inreg %{{.*}}, i32 inreg %{{.*}}, i32 inreg %{{.*}})
void pass_s12(struct S12 s) {}

// 16-byte struct: four registers (max for register passing).
struct S16 { int a; int b; int c; int d; };
// CHECK-LABEL: define{{.*}} void @pass_s16(i32 inreg %{{.*}}, i32 inreg %{{.*}}, i32 inreg %{{.*}}, i32 inreg %{{.*}})
void pass_s16(struct S16 s) {}

// 20-byte struct: too large, passed byval on stack.
struct S20 { int a; int b; int c; int d; int e; };
// CHECK-LABEL: define{{.*}} void @pass_s20(ptr noundef byval(%struct.S20) align 4 %s)
void pass_s20(struct S20 s) {}

// Struct with float: floats in structs still go in GPRs.
struct SF { float f; int i; };
// CHECK-LABEL: define{{.*}} void @pass_sf(i32 inreg %{{.*}}, i32 inreg %{{.*}})
void pass_sf(struct SF s) {}

// Return 1-word struct in R0.
// CHECK-LABEL: define{{.*}} i32 @ret_s4()
struct S4 ret_s4(void) { struct S4 s = {42}; return s; }

// Return 2-word struct in R0:R1.
// CHECK-LABEL: define{{.*}} { i32, i32 } @ret_s8()
struct S8 ret_s8(void) { struct S8 s = {1, 2}; return s; }

// Return large struct via sret pointer.
// CHECK-LABEL: define{{.*}} void @ret_s20(ptr dead_on_unwind noalias writable sret(%struct.S20) align 4 %agg.result)
struct S20 ret_s20(void) { struct S20 s = {1,2,3,4,5}; return s; }

// Struct + scalar mixed: struct takes registers, scalar takes remaining.
// CHECK-LABEL: define{{.*}} void @mixed_struct_scalar(i32 inreg %{{.*}}, i32 inreg %{{.*}}, i32 inreg noundef %x)
void mixed_struct_scalar(struct S8 s, int x) {}

// Large struct + scalar: large struct is byval, scalar uses register.
// CHECK-LABEL: define{{.*}} void @large_then_scalar(ptr noundef byval(%struct.S20) align 4 %s, i32 inreg noundef %x)
void large_then_scalar(struct S20 s, int x) {}
