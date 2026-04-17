// RUN: %clang_cc1 -triple sh-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// Test basic scalar argument passing (R4-R7)
// CHECK-LABEL: define{{.*}} void @scalar_args(i32 inreg noundef %a, i32 inreg noundef %b, i32 inreg noundef %c, i32 inreg noundef %d)
void scalar_args(int a, int b, int c, int d) {}

// Test scalar + stack overflow
// CHECK-LABEL: define{{.*}} void @scalar_overflow(i32 inreg noundef %a, i32 inreg noundef %b, i32 inreg noundef %c, i32 inreg noundef %d, i32 noundef %e)
void scalar_overflow(int a, int b, int c, int d, int e) {}

// Test small struct (1 word) - passed in register
struct Small1 { int x; };
// CHECK-LABEL: define{{.*}} void @small_struct_1(i32 inreg %s.coerce)
void small_struct_1(struct Small1 s) {}

// Test small struct (2 words) - passed in registers
struct Small2 { int x; int y; };
// CHECK-LABEL: define{{.*}} void @small_struct_2(i32 inreg %{{.*}}, i32 inreg %{{.*}})
void small_struct_2(struct Small2 s) {}

// Test medium struct (4 words / 16 bytes) - passed in registers
struct Medium { int a; int b; int c; int d; };
// CHECK-LABEL: define{{.*}} void @medium_struct(i32 inreg %{{.*}}, i32 inreg %{{.*}}, i32 inreg %{{.*}}, i32 inreg %{{.*}})
void medium_struct(struct Medium s) {}

// Test large struct (> 16 bytes) - passed indirectly
struct Large { int a; int b; int c; int d; int e; };
// CHECK-LABEL: define{{.*}} void @large_struct(ptr noundef byval(%struct.Large) align 4 %s)
void large_struct(struct Large s) {}

// Test return value - integer
// CHECK-LABEL: define{{.*}} i32 @ret_int()
int ret_int(void) { return 42; }

// Test return value - small struct (fits in R0)
// CHECK-LABEL: define{{.*}} i32 @ret_small()
struct Small1 ret_small(void) { struct Small1 s = {1}; return s; }

// Test return value - 2-word struct (fits in R0:R1)
// CHECK-LABEL: define{{.*}} { i32, i32 } @ret_small2()
struct Small2 ret_small2(void) { struct Small2 s = {1, 2}; return s; }

// Test return value - large struct (via sret)
// CHECK-LABEL: define{{.*}} void @ret_large(ptr dead_on_unwind noalias writable sret(%struct.Large) align 4 %agg.result)
struct Large ret_large(void) { struct Large s = {1,2,3,4,5}; return s; }

// Test empty struct - should be ignored
struct Empty {};
// CHECK-LABEL: define{{.*}} void @empty_struct()
void empty_struct(struct Empty e) {}

// Test struct with register exhaustion - first struct uses registers, rest goes to stack
// CHECK-LABEL: define{{.*}} void @mixed_args(i32 inreg %{{.*}}, i32 inreg %{{.*}}, i32 inreg noundef %x, i32 inreg noundef %y)
void mixed_args(struct Small2 s, int x, int y) {}

// Test i8/i16 promotion
// CHECK-LABEL: define{{.*}} void @promote_args(i8 inreg noundef %a, i16 inreg noundef %b)
void promote_args(char a, short b) {}
